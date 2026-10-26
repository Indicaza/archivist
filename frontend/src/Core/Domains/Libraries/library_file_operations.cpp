#include "library_file_operations.h"

#include "library_file_policy.h"
#include "library_file_repository.h"
#include "library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

bool isInsideRoot(const QString &rootPath, const QString &candidatePath)
{
    const QString root = QDir::cleanPath(rootPath);
    const QString candidate = QDir::cleanPath(candidatePath);
    return candidate == root || candidate.startsWith(root == QStringLiteral("/") ? root : root + QLatin1Char('/'));
}

QString relativeTo(const QString &rootPath, const QString &absolutePath)
{
    const QString root = QDir::cleanPath(rootPath);
    const QString candidate = QDir::cleanPath(absolutePath);
    return candidate == root ? QString() : candidate.mid(root == QStringLiteral("/") ? 1 : root.size() + 1);
}

QString joinPath(const QString &base, const QString &relative)
{
    return relative.isEmpty() ? QDir::cleanPath(base) : QDir::cleanPath(base + QLatin1Char('/') + relative);
}

qsizetype lineCount(const QString &content)
{
    if (content.isEmpty()) {
        return 0;
    }

    qsizetype count = 1;

    for (qsizetype index = 0; index < content.size(); ++index) {
        if (content.at(index) == QLatin1Char('\r')) {
            ++count;

            if (index + 1 < content.size() && content.at(index + 1) == QLatin1Char('\n')) {
                ++index;
            }
        } else if (content.at(index) == QLatin1Char('\n')) {
            ++count;
        }
    }

    return count;
}

// libraryEntryNameSchema plus validatedName(): trimmed, 1..255 characters,
// not "." or "..", no slashes or control characters.
QString validatedName(const QString &value, const QString &invalidMessage)
{
    const QString name = JsCompat::trimmed(value);
    static const QRegularExpression controlCharacters(QStringLiteral("[\\x{0000}-\\x{001f}]"));

    if (name.isEmpty() || name.size() > 255 || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        throw CoreError(invalidMessage);
    }

    if (controlCharacters.match(name).hasMatch()) {
        throw CoreError(QStringLiteral("Invalid file or folder name."));
    }

    return name;
}

QString stripSlashes(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    static const QRegularExpression edges(QStringLiteral("^/+|/+$"));
    value.replace(edges, QString());
    return value;
}

// normalizedRelativePath(): rejects empty, "." and ".." segments.
QString normalizedRelativePath(const QString &value)
{
    const QString normalized = stripSlashes(JsCompat::trimmed(value));

    if (normalized.isEmpty()) {
        return {};
    }

    const QStringList parts = normalized.split(QLatin1Char('/'));

    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) {
            throw CoreError(QStringLiteral("Invalid Library path."));
        }
    }

    return parts.join(QLatin1Char('/'));
}

QVariantMap requireLibrary(CoreDatabase &database, const QString &libraryId)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    return *library;
}

QString resolveLibraryRoot(CoreDatabase &database, const QString &libraryId)
{
    const QVariantMap library = requireLibrary(database, libraryId);

    if (!isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Archived Libraries cannot modify files."));
    }

    const QString rootPath = QFileInfo(library.value(QStringLiteral("rootPath")).toString()).canonicalFilePath();

    if (rootPath.isEmpty()) {
        throw CoreError(QStringLiteral("The Library folder could not be resolved."));
    }

    return rootPath;
}

QString resolveParentDirectory(const QString &rootPath, const QString &relativeDirectory)
{
    const QString requested = joinPath(rootPath, normalizedRelativePath(relativeDirectory));

    if (!isInsideRoot(rootPath, requested)) {
        throw CoreError(QStringLiteral("The requested directory escaped the Library root."));
    }

    const QString canonical = QFileInfo(requested).canonicalFilePath();
    const QFileInfo info(canonical);

    if (canonical.isEmpty() || !info.isDir() || info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("The destination folder does not exist."));
    }

    if (!isInsideRoot(rootPath, canonical)) {
        throw CoreError(QStringLiteral("The destination folder resolved outside the Library root."));
    }

    return canonical;
}

struct CatalogedFile
{
    QVariantMap file;
    QString rootPath;
    QString absolutePath;
};

CatalogedFile resolveCatalogedFile(CoreDatabase &database, const QString &libraryId, const QString &fileId)
{
    const QString rootPath = resolveLibraryRoot(database, libraryId);
    const auto file = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!file || file->value(QStringLiteral("status")).toString() != QStringLiteral("available")) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    const QString requested = joinPath(rootPath, file->value(QStringLiteral("relativePath")).toString());

    if (!isInsideRoot(rootPath, requested)) {
        throw CoreError(QStringLiteral("The requested file escaped the Library root."));
    }

    const QString absolutePath = QFileInfo(requested).canonicalFilePath();
    const QFileInfo info(absolutePath);

    if (absolutePath.isEmpty() || !info.isFile() || info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("The file no longer exists."));
    }

    if (!isInsideRoot(rootPath, absolutePath)) {
        throw CoreError(QStringLiteral("The requested file resolved outside the Library root."));
    }

    return {*file, rootPath, absolutePath};
}

void ensureDestinationAvailable(const QString &destinationPath)
{
    const QFileInfo info(destinationPath);

    if (info.exists() || info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("A file or folder with that name already exists."));
    }
}

QVariantMap insertCatalogFile(CoreDatabase &database, const QString &libraryId, const QString &relativePath, const QString &name, const QFileInfo &info)
{
    const QString fileId = newId();

    database
        .prepare(QStringLiteral(
                     "INSERT INTO library_files (id, library_id, relative_path, name, extension, size_bytes, modified_at, "
                     "status, last_seen_at) VALUES (?, ?, ?, ?, ?, ?, ?, 'available', %1)"
        )
                     .arg(nowSql))
        .bind(1, fileId)
        .bind(2, libraryId)
        .bind(3, relativePath)
        .bind(4, name)
        .bind(5, LibraryFilePolicy::extensionOf(name))
        .bind(6, info.size())
        .bind(7, JsCompat::isoTimestamp(info.lastModified()))
        .run();

    const auto file = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!file) {
        throw CoreError(QStringLiteral("The new Library file could not be loaded."));
    }

    return *file;
}
}

namespace LibraryFileOperations
{
QVariantMap writeText(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fileId,
    const QString &content,
    const QString &expectedModifiedAt
)
{
    if (QDateTime::fromString(expectedModifiedAt, Qt::ISODateWithMs).isNull()) {
        throw CoreError(QStringLiteral("Invalid file content."));
    }

    const QVariantMap library = requireLibrary(database, libraryId);

    if (!isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Archived Libraries cannot edit files."));
    }

    const auto file = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!file) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    const QString status = file->value(QStringLiteral("status")).toString();

    if (status != QStringLiteral("available")) {
        throw CoreError(QStringLiteral("This file is marked %1. Rescan the Library before editing it.").arg(status));
    }

    if (!LibraryFilePolicy::isSupportedTextFile(
            file->value(QStringLiteral("name")).toString(),
            file->value(QStringLiteral("extension")).toString()
        )) {
        throw CoreError(QStringLiteral("This file type is not supported for text editing."));
    }

    const QByteArray bytes = content.toUtf8();

    if (bytes.size() > LibraryFilePolicy::maxTextPreviewBytes) {
        throw CoreError(
            QStringLiteral("This file is too large to save. The current limit is %1 KB.")
                .arg(std::lround(LibraryFilePolicy::maxTextPreviewBytes / 1024.0)),
            {{QStringLiteral("sizeBytes"), bytes.size()}, {QStringLiteral("maximumBytes"), LibraryFilePolicy::maxTextPreviewBytes}}
        );
    }

    const QString rootPath = QFileInfo(library.value(QStringLiteral("rootPath")).toString()).canonicalFilePath();

    if (rootPath.isEmpty()) {
        throw CoreError(QStringLiteral("The Library folder could not be resolved."));
    }

    const QString absolutePath = joinPath(rootPath, file->value(QStringLiteral("relativePath")).toString());

    if (!isInsideRoot(rootPath, absolutePath)) {
        throw CoreError(QStringLiteral("The requested file escaped the Library root."));
    }

    const QFileInfo info(absolutePath);

    if (!info.exists() && !info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("The file no longer exists. Rescan the Library to refresh its catalog."));
    }

    if (info.isSymbolicLink() || !info.isFile()) {
        throw CoreError(QStringLiteral("The cataloged path is no longer a regular file."));
    }

    const QString currentModifiedAt = JsCompat::isoTimestamp(info.lastModified());

    if (currentModifiedAt != expectedModifiedAt) {
        throw CoreError(
            QStringLiteral("This file changed on disk after it was opened."),
            {{QStringLiteral("expectedModifiedAt"), expectedModifiedAt}, {QStringLiteral("currentModifiedAt"), currentModifiedAt}}
        );
    }

    const QString canonicalFile = info.canonicalFilePath();

    if (canonicalFile.isEmpty()) {
        throw CoreError(QStringLiteral("The file could not be resolved."));
    }

    if (!isInsideRoot(rootPath, canonicalFile)) {
        throw CoreError(QStringLiteral("The requested file resolved outside the Library root."));
    }

    if (!QFileInfo(canonicalFile).isWritable()) {
        throw CoreError(QStringLiteral("Archivist cannot write this file."));
    }

    const QFileDevice::Permissions permissions = QFile::permissions(canonicalFile);
    QSaveFile output(canonicalFile);
    output.setDirectWriteFallback(false);

    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        throw CoreError(QStringLiteral("The file could not be saved."), {{QStringLiteral("cause"), output.errorString()}});
    }

    QFile::setPermissions(canonicalFile, permissions);

    const QFileInfo saved(canonicalFile);
    database
        .prepare(QStringLiteral(
                     "UPDATE library_files SET size_bytes = ?, modified_at = ?, status = 'available', updated_at = %1 "
                     "WHERE id = ? AND library_id = ?"
        )
                     .arg(nowSql))
        .bind(1, saved.size())
        .bind(2, JsCompat::isoTimestamp(saved.lastModified()))
        .bind(3, fileId)
        .bind(4, libraryId)
        .run();

    const auto savedFile = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!savedFile) {
        throw CoreError(QStringLiteral("The saved Library file could not be loaded."));
    }

    return {
        {QStringLiteral("file"), *savedFile},
        {QStringLiteral("content"), content},
        {QStringLiteral("encoding"), QStringLiteral("utf-8")},
        {QStringLiteral("lineCount"), lineCount(content)},
        {QStringLiteral("readAt"), JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc())},
    };
}

QVariantMap moveFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &targetDirectory)
{
    if (JsCompat::trimmed(targetDirectory).size() > 2000) {
        throw CoreError(QStringLiteral("Invalid file destination."));
    }

    const auto library = LibraryRepository::libraryById(database, libraryId);
    const auto file = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    if (!file) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    if (file->value(QStringLiteral("status")).toString() != QStringLiteral("available")) {
        throw CoreError(QStringLiteral("Only available files can be moved."));
    }

    const QString rootPath = library->value(QStringLiteral("rootPath")).toString();
    auto resolveInside = [&](const QString &relativePath) {
        const QString resolved = joinPath(rootPath, relativePath);
        const QString offset = relativeTo(rootPath, resolved);

        if (!isInsideRoot(rootPath, resolved) || offset.startsWith(QStringLiteral(".."))) {
            throw CoreError(QStringLiteral("The destination must stay inside the Library."));
        }

        return resolved;
    };

    const QString name = file->value(QStringLiteral("name")).toString();
    const QString sourcePath = resolveInside(file->value(QStringLiteral("relativePath")).toString());
    const QString destinationDirectory = resolveInside(stripSlashes(JsCompat::trimmed(targetDirectory)));
    const QString destinationPath = joinPath(destinationDirectory, name);

    if (!QFileInfo(destinationDirectory).isDir()) {
        throw CoreError(QStringLiteral("The destination folder does not exist."));
    }

    if (sourcePath == destinationPath) {
        return *file;
    }

    if (QFileInfo::exists(destinationPath)) {
        throw CoreError(QStringLiteral("A file named %1 already exists there.").arg(name));
    }

    if (!QFile::rename(sourcePath, destinationPath)) {
        throw CoreError(QStringLiteral("The file could not be moved."));
    }

    return LibraryFileRepository::updateFileLocation(
        database,
        libraryId,
        fileId,
        relativeTo(rootPath, destinationPath),
        QFileInfo(destinationPath).fileName(),
        LibraryFilePolicy::extensionOf(QFileInfo(destinationPath).fileName())
    );
}

QVariantMap createEntry(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &parentDirectory,
    const QString &name,
    bool directory
)
{
    if (JsCompat::trimmed(parentDirectory).size() > 2000) {
        throw CoreError(QStringLiteral("Invalid file or folder request."));
    }

    const QString validName = validatedName(name, QStringLiteral("Invalid file or folder request."));
    const QString rootPath = resolveLibraryRoot(database, libraryId);
    const QString parentPath = resolveParentDirectory(rootPath, parentDirectory);
    const QString destinationPath = joinPath(parentPath, validName);

    if (!isInsideRoot(rootPath, destinationPath)) {
        throw CoreError(QStringLiteral("The new entry escaped the Library root."));
    }

    ensureDestinationAvailable(destinationPath);
    const QString relativePath = relativeTo(rootPath, destinationPath);

    if (directory) {
        if (!QDir().mkdir(destinationPath)) {
            throw CoreError(QStringLiteral("The folder could not be created."));
        }

        return {
            {QStringLiteral("kind"), QStringLiteral("directory")},
            {QStringLiteral("relativePath"), relativePath},
            {QStringLiteral("name"), validName},
            {QStringLiteral("file"), QVariant::fromValue(nullptr)},
        };
    }

    QFile created(destinationPath);

    if (!created.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        throw CoreError(QStringLiteral("A file or folder with that name already exists."));
    }

    created.close();

    return {
        {QStringLiteral("kind"), QStringLiteral("file")},
        {QStringLiteral("relativePath"), relativePath},
        {QStringLiteral("name"), validName},
        {QStringLiteral("file"), insertCatalogFile(database, libraryId, relativePath, validName, QFileInfo(destinationPath))},
    };
}

QVariantMap renameFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &name)
{
    const QString validName = validatedName(name, QStringLiteral("Invalid file name."));
    const CatalogedFile cataloged = resolveCatalogedFile(database, libraryId, fileId);
    const QString destinationPath = joinPath(QFileInfo(cataloged.absolutePath).absolutePath(), validName);

    if (destinationPath != cataloged.absolutePath) {
        // A case-only rename on a case-insensitive disk points at the same file.
        if (QFileInfo(destinationPath).canonicalFilePath() != cataloged.absolutePath) {
            ensureDestinationAvailable(destinationPath);
        }

        if (!QFile::rename(cataloged.absolutePath, destinationPath)) {
            throw CoreError(QStringLiteral("The file could not be renamed."));
        }
    }

    return LibraryFileRepository::updateFileLocation(
        database,
        libraryId,
        fileId,
        relativeTo(cataloged.rootPath, destinationPath),
        validName,
        LibraryFilePolicy::extensionOf(validName)
    );
}

QVariantMap duplicateFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &name)
{
    const QString validName = validatedName(name, QStringLiteral("Invalid duplicate file name."));
    const CatalogedFile cataloged = resolveCatalogedFile(database, libraryId, fileId);
    const QString destinationPath = joinPath(QFileInfo(cataloged.absolutePath).absolutePath(), validName);

    ensureDestinationAvailable(destinationPath);

    if (!QFile::copy(cataloged.absolutePath, destinationPath)) {
        throw CoreError(QStringLiteral("The file could not be duplicated."));
    }

    return insertCatalogFile(
        database,
        libraryId,
        relativeTo(cataloged.rootPath, destinationPath),
        validName,
        QFileInfo(destinationPath)
    );
}

void revealEntry(CoreDatabase &database, const QString &libraryId, const QString &relativePath)
{
    if (JsCompat::trimmed(relativePath).size() > 2000) {
        throw CoreError(QStringLiteral("Invalid reveal path."));
    }

    const QString rootPath = resolveLibraryRoot(database, libraryId);
    const QString requestedPath = joinPath(rootPath, normalizedRelativePath(relativePath));

    if (!isInsideRoot(rootPath, requestedPath)) {
        throw CoreError(QStringLiteral("The requested entry escaped the Library root."));
    }

    const QString targetPath = QFileInfo(requestedPath).canonicalFilePath();

    if (targetPath.isEmpty()) {
        throw CoreError(QStringLiteral("The requested entry no longer exists."));
    }

    if (!isInsideRoot(rootPath, targetPath)) {
        throw CoreError(QStringLiteral("The requested entry resolved outside the Library root."));
    }

#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), targetPath});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(targetPath)});
#else
    const QFileInfo target(targetPath);
    QProcess::startDetached(QStringLiteral("xdg-open"), {target.isDir() ? targetPath : target.absolutePath()});
#endif
}
}
