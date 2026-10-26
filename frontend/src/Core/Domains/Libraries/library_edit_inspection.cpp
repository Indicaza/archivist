#include "library_edit_inspection.h"

#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"
#include "library_file_policy.h"
#include "library_repository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

using namespace CoreValues;

namespace
{
const QString unsafePath = QStringLiteral("AI edit paths must be safe Library-relative paths.");
const QString escapedRoot = QStringLiteral("The requested edit path escaped the Library root.");

QString parentOf(const QString &relativePath)
{
    const qsizetype slash = relativePath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : relativePath.left(slash);
}

QString baseName(const QString &relativePath)
{
    return relativePath.mid(relativePath.lastIndexOf(QLatin1Char('/')) + 1);
}

QString joined(const QString &root, const QString &relative)
{
    return relative.isEmpty() ? QDir::cleanPath(root) : QDir::cleanPath(root + QLatin1Char('/') + relative);
}

// Symlink-aware existence check, like lstat().catch(() => null).
bool pathExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymbolicLink();
}

QString canonicalParent(const QString &rootPath, const QString &relativePath)
{
    const QString requestedParent = joined(rootPath, parentOf(relativePath));

    if (!LibraryEditInspection::isInsideRoot(rootPath, requestedParent)) {
        throw CoreError(escapedRoot);
    }

    const QString canonical = QFileInfo(requestedParent).canonicalFilePath();

    if (canonical.isEmpty()) {
        throw CoreError(QStringLiteral("The destination folder does not exist."));
    }

    if (!LibraryEditInspection::isInsideRoot(rootPath, canonical)) {
        throw CoreError(QStringLiteral("The destination folder resolved outside the Library root."));
    }

    const QFileInfo info(canonical);

    if (!info.isDir()) {
        throw CoreError(QStringLiteral("The destination parent is not a directory."));
    }

    if (!info.isWritable()) {
        throw CoreError(QStringLiteral("Archivist cannot write to the destination folder."));
    }

    return canonical;
}
}

namespace LibraryEditInspection
{
bool isAbsoluteLike(const QString &value)
{
    if (value.startsWith(QLatin1Char('/')) || value.startsWith(QLatin1Char('\\'))) {
        return true;
    }

    // Windows drive paths such as C:\ or C:/
    return value.size() >= 3 && value.at(0).isLetter() && value.at(0).unicode() < 128 && value.at(1) == QLatin1Char(':')
        && (value.at(2) == QLatin1Char('/') || value.at(2) == QLatin1Char('\\'));
}

QString normalizeRelativePath(const QString &value, const QString &message)
{
    const QString trimmed = JsCompat::trimmed(value);
    bool hasControl = false;

    for (const QChar character : trimmed) {
        hasControl = hasControl || character.unicode() < 0x20;
    }

    if (trimmed.isEmpty() || isAbsoluteLike(trimmed) || hasControl) {
        throw CoreError(message);
    }

    QString normalized = trimmed;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = normalized.split(QLatin1Char('/'));

    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) {
            throw CoreError(message);
        }
    }

    return parts.join(QLatin1Char('/'));
}

bool isInsideRoot(const QString &rootPath, const QString &candidatePath)
{
    const QString root = QDir::cleanPath(rootPath);
    const QString candidate = QDir::cleanPath(candidatePath);
    return candidate == root || candidate.startsWith(root == QStringLiteral("/") ? root : root + QLatin1Char('/'));
}

QString canonicalLibraryRoot(CoreDatabase &database, const QString &libraryId)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    if (!isNullish(library->value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Archived Libraries cannot accept AI edit proposals."));
    }

    const QString root = QFileInfo(library->value(QStringLiteral("rootPath")).toString()).canonicalFilePath();

    if (root.isEmpty()) {
        throw CoreError(QStringLiteral("The Library folder could not be resolved."));
    }

    return root;
}

CreateTarget createTarget(CoreDatabase &database, const QString &libraryId, const QString &requestedPath)
{
    const QString relativePath = normalizeRelativePath(requestedPath, unsafePath);
    const QString rootPath = canonicalLibraryRoot(database, libraryId);
    const QString parent = canonicalParent(rootPath, relativePath);
    const QString name = baseName(relativePath);

    if (!LibraryFilePolicy::isCatalogableFile(name)
        || !LibraryFilePolicy::isSupportedTextFile(name, LibraryFilePolicy::extensionOf(name))) {
        throw CoreError(QStringLiteral("This file type is not supported for AI text creation."));
    }

    const QString destination = joined(parent, name);

    if (!isInsideRoot(rootPath, destination)) {
        throw CoreError(escapedRoot);
    }

    if (pathExists(destination)) {
        throw CoreError(QStringLiteral("The proposed create target already exists."));
    }

    return {relativePath, parentOf(relativePath), name, QStringLiteral("absent")};
}

CreateTarget directoryCreateTarget(CoreDatabase &database, const QString &libraryId, const QString &requestedPath)
{
    const QString relativePath = normalizeRelativePath(requestedPath, unsafePath);
    const QString rootPath = canonicalLibraryRoot(database, libraryId);
    const QString parent = canonicalParent(rootPath, relativePath);
    const QString name = baseName(relativePath);

    if (!LibraryFilePolicy::isCatalogableDirectory(name)) {
        throw CoreError(QStringLiteral("This folder name is not supported for AI directory creation."));
    }

    const QString destination = joined(parent, name);

    if (!isInsideRoot(rootPath, destination)) {
        throw CoreError(escapedRoot);
    }

    if (pathExists(destination)) {
        throw CoreError(QStringLiteral("The proposed directory target already exists."));
    }

    return {relativePath, parentOf(relativePath), name, QStringLiteral("absent")};
}

EmptyDirectory emptyDirectory(CoreDatabase &database, const QString &libraryId, const QString &requestedPath)
{
    const QString relativePath = normalizeRelativePath(requestedPath, unsafePath);
    const QString rootPath = canonicalLibraryRoot(database, libraryId);
    const QString parent = canonicalParent(rootPath, relativePath);
    const QString name = baseName(relativePath);
    const QString requested = joined(parent, name);

    if (!isInsideRoot(rootPath, requested)) {
        throw CoreError(escapedRoot);
    }

    const QFileInfo info(requested);

    if (!pathExists(requested)) {
        throw CoreError(QStringLiteral("The AI edit directory no longer exists."));
    }

    if (info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("AI edits do not follow symbolic-link directories."));
    }

    if (!info.isDir()) {
        throw CoreError(QStringLiteral("The AI edit directory is no longer a directory."));
    }

    const QString canonical = info.canonicalFilePath();

    if (canonical.isEmpty()) {
        throw CoreError(QStringLiteral("The AI edit directory could not be resolved."));
    }

    if (!isInsideRoot(rootPath, canonical)) {
        throw CoreError(QStringLiteral("The AI edit directory resolved outside the Library root."));
    }

    QDir directory(canonical);

    if (!directory.isReadable()) {
        throw CoreError(QStringLiteral("Archivist cannot inspect the AI edit directory."));
    }

    if (!directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) {
        throw CoreError(QStringLiteral("The AI edit directory is no longer empty."));
    }

    return {relativePath, name, QStringLiteral("empty-directory")};
}

TextFile textFile(CoreDatabase &database, const QString &libraryId, const QString &requestedPath)
{
    const QString relativePath = normalizeRelativePath(requestedPath, unsafePath);
    const QString rootPath = canonicalLibraryRoot(database, libraryId);
    const QString requested = joined(rootPath, relativePath);

    if (!isInsideRoot(rootPath, requested)) {
        throw CoreError(escapedRoot);
    }

    const QFileInfo info(requested);

    if (!pathExists(requested)) {
        throw CoreError(QStringLiteral("The proposed edit source does not exist."));
    }

    if (info.isSymbolicLink()) {
        throw CoreError(QStringLiteral("AI edits do not follow symbolic-link files."));
    }

    if (!info.isFile()) {
        throw CoreError(QStringLiteral("The proposed edit source is not a regular file."));
    }

    const QString name = baseName(relativePath);

    if (!LibraryFilePolicy::isSupportedTextFile(name, LibraryFilePolicy::extensionOf(name))) {
        throw CoreError(QStringLiteral("This file type is not supported for AI text edits."));
    }

    if (info.size() > LibraryFilePolicy::maxTextPreviewBytes) {
        throw CoreError(
            QStringLiteral("This file exceeds the current safe AI text-edit limit."),
            {{QStringLiteral("sizeBytes"), info.size()}, {QStringLiteral("maximumBytes"), LibraryFilePolicy::maxTextPreviewBytes}}
        );
    }

    const QString canonical = info.canonicalFilePath();

    if (canonical.isEmpty()) {
        throw CoreError(QStringLiteral("The proposed edit source could not be resolved."));
    }

    if (!isInsideRoot(rootPath, canonical)) {
        throw CoreError(QStringLiteral("The proposed edit source resolved outside the Library root."));
    }

    QFile handle(canonical);

    if (!info.isReadable() || !handle.open(QIODevice::ReadOnly)) {
        throw CoreError(QStringLiteral("Archivist cannot read the proposed edit source."));
    }

    const QByteArray bytes = handle.readAll();

    if (bytes.contains('\0')) {
        throw CoreError(QStringLiteral("The proposed edit source appears to be binary."));
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString content = decoder(bytes);

    if (decoder.hasError()) {
        throw CoreError(QStringLiteral("The proposed edit source is not valid UTF-8 text."));
    }

    return {
        relativePath,
        content,
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
        bytes.size(),
        JsCompat::isoTimestamp(info.lastModified()),
    };
}
}
