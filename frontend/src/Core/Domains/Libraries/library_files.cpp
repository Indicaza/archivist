#include "library_files.h"

#include "library_file_policy.h"
#include "library_file_repository.h"
#include "library_repository.h"
#include "library_text_chunker.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QCollator>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <functional>

using namespace CoreValues;

namespace
{
constexpr int maxReportedIssues = 100;

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

    if (candidate == root) {
        return {};
    }

    return candidate.mid(root == QStringLiteral("/") ? 1 : root.size() + 1);
}

QString joinPath(const QString &base, const QString &relative)
{
    return relative.isEmpty() ? base : QDir::cleanPath(base + QLatin1Char('/') + relative);
}

QString joinRelative(const QString &directory, const QString &name)
{
    return directory.isEmpty() ? name : directory + QLatin1Char('/') + name;
}

QVariantMap requireLibrary(CoreDatabase &database, const QString &libraryId)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    return *library;
}

// --- Scanner -----------------------------------------------------------------

struct ScanAccumulator
{
    QList<LibraryFileRepository::ScannedFile> files;
    QVariantList issues;
    int discoveredFileCount = 0;
    int ignoredEntryCount = 0;
    int errorCount = 0;
};

void addIssue(ScanAccumulator &accumulator, const QString &relativePath, const QString &message)
{
    ++accumulator.errorCount;

    if (accumulator.issues.size() < maxReportedIssues) {
        accumulator.issues.append(QVariantMap{
            {QStringLiteral("relativePath"), relativePath.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(relativePath)},
            {QStringLiteral("message"), message},
        });
    }
}

QString resolveInsideRoot(const QString &rootPath, const QString &relativePath)
{
    const QString absolutePath = joinPath(rootPath, relativePath);

    if (!isInsideRoot(rootPath, absolutePath)) {
        throw CoreError(QStringLiteral("A scanned path escaped the Library root."));
    }

    return absolutePath;
}

QDir::Filters entryFilters()
{
    return QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
}

void scanDirectory(const QString &rootPath, const QString &relativeDirectory, ScanAccumulator &accumulator);

void scanManagedPhotoLibrary(const QString &rootPath, const QString &relativePackagePath, ScanAccumulator &accumulator)
{
    for (const QString &directoryName : {QStringLiteral("originals"), QStringLiteral("Originals"), QStringLiteral("Masters")}) {
        const QString relativeOriginals = joinRelative(relativePackagePath, directoryName);
        const QFileInfo originals(resolveInsideRoot(rootPath, relativeOriginals));

        if (originals.exists() && originals.isDir() && !originals.isSymbolicLink()) {
            scanDirectory(rootPath, relativeOriginals, accumulator);
            return;
        }
    }

    ++accumulator.ignoredEntryCount;
}

void scanDirectory(const QString &rootPath, const QString &relativeDirectory, ScanAccumulator &accumulator)
{
    const QString absoluteDirectory = resolveInsideRoot(rootPath, relativeDirectory);
    const QFileInfo directoryInfo(absoluteDirectory);

    if (!directoryInfo.isReadable() || !directoryInfo.isExecutable()) {
        addIssue(
            accumulator,
            relativeDirectory,
            QStringLiteral("EACCES: permission denied, opendir '%1'").arg(absoluteDirectory)
        );
        return;
    }

    QDirIterator iterator(absoluteDirectory, entryFilters());

    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo entry = iterator.fileInfo();
        const QString name = entry.fileName();
        const QString relativeEntry = joinRelative(relativeDirectory, name);

        if (entry.isSymbolicLink()) {
            ++accumulator.ignoredEntryCount;
            continue;
        }

        if (entry.isDir()) {
            if (LibraryFilePolicy::isManagedPhotoLibraryDirectory(name)) {
                scanManagedPhotoLibrary(rootPath, relativeEntry, accumulator);
                continue;
            }

            if (LibraryFilePolicy::isIgnoredScanDirectory(name) || !LibraryFilePolicy::isCatalogableDirectory(name)) {
                ++accumulator.ignoredEntryCount;
                continue;
            }

            scanDirectory(rootPath, relativeEntry, accumulator);
            continue;
        }

        if (!entry.isFile()) {
            ++accumulator.ignoredEntryCount;
            continue;
        }

        if (LibraryFilePolicy::isIgnoredFile(name) || !LibraryFilePolicy::isCatalogableFile(name)) {
            ++accumulator.ignoredEntryCount;
            continue;
        }

        ++accumulator.discoveredFileCount;
        const QString absoluteFile = resolveInsideRoot(rootPath, relativeEntry);
        QString status = QStringLiteral("available");

        if (!entry.isReadable()) {
            status = QStringLiteral("unreadable");
            addIssue(accumulator, relativeEntry, QStringLiteral("EACCES: permission denied, access '%1'").arg(absoluteFile));
        }

        accumulator.files.append({
            relativeEntry,
            name,
            LibraryFilePolicy::extensionOf(name),
            entry.size(),
            JsCompat::isoTimestamp(entry.lastModified()),
            status,
        });
    }
}

void validateLibraryRoot(const QString &rootPath)
{
    const QFileInfo root(rootPath);

    if (!root.exists() && !root.isSymbolicLink()) {
        throw CoreError(QStringLiteral("The Library folder no longer exists."));
    }

    if (root.isSymbolicLink() || !root.isDir()) {
        throw CoreError(QStringLiteral("The Library root is no longer a folder."));
    }

    if (!root.isReadable()) {
        throw CoreError(QStringLiteral("Archivist cannot read the Library folder."));
    }
}

QVariantMap failedIndexSummary(const QString &libraryId, const QString &message)
{
    return {
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("processedFileCount"), 0},
        {QStringLiteral("unchangedFileCount"), 0},
        {QStringLiteral("indexedFileCount"), 0},
        {QStringLiteral("emptyFileCount"), 0},
        {QStringLiteral("unavailableFileCount"), 0},
        {QStringLiteral("failedFileCount"), 1},
        {QStringLiteral("chunkCount"), 0},
        {QStringLiteral("durationMs"), 0},
        {QStringLiteral("issues"),
         QVariantList{QVariantMap{
             {QStringLiteral("fileId"), QString()},
             {QStringLiteral("relativePath"), QString()},
             {QStringLiteral("message"), message},
         }}},
    };
}

QString errorText(const std::exception &error)
{
    if (const auto *coreError = dynamic_cast<const CoreError *>(&error)) {
        return coreError->message();
    }

    const QString message = QString::fromUtf8(error.what()).trimmed();
    return message.isEmpty() ? QStringLiteral("An unknown filesystem error occurred.") : message;
}

// --- Git -----------------------------------------------------------------------

struct GitResult
{
    bool started = false;
    int exitCode = -1;
    QString output;
    QString errors;
};

GitResult runGit(const QStringList &arguments)
{
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);

    GitResult result;
    result.started = process.waitForStarted(5000);

    if (!result.started) {
        result.errors = process.errorString();
        return result;
    }

    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        result.errors = QStringLiteral("git timed out");
        return result;
    }

    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.errors = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QString classifyGitStatus(QChar recordType, const QString &indexStatus, const QString &worktreeStatus)
{
    const QString pair = indexStatus + worktreeStatus;

    if (recordType == QLatin1Char('u') || indexStatus == QStringLiteral("U") || worktreeStatus == QStringLiteral("U")
        || pair == QStringLiteral("AA") || pair == QStringLiteral("DD")) {
        return QStringLiteral("conflicted");
    }

    if (recordType == QLatin1Char('!')) {
        return QStringLiteral("ignored");
    }

    if (recordType == QLatin1Char('?')) {
        return QStringLiteral("untracked");
    }

    if (indexStatus == QStringLiteral("C") || worktreeStatus == QStringLiteral("C")) {
        return QStringLiteral("added");
    }

    if (recordType == QLatin1Char('2') || indexStatus == QStringLiteral("R") || worktreeStatus == QStringLiteral("R")) {
        return QStringLiteral("renamed");
    }

    if (indexStatus == QStringLiteral("D") || worktreeStatus == QStringLiteral("D")) {
        return QStringLiteral("deleted");
    }

    if (indexStatus == QStringLiteral("A") || worktreeStatus == QStringLiteral("A")) {
        return QStringLiteral("added");
    }

    return QStringLiteral("modified");
}

QVariant gitLibraryPath(const QString &repositoryRoot, const QString &libraryRoot, const QString &repositoryRelativePath)
{
    const QString absolutePath = QDir::cleanPath(repositoryRoot + QLatin1Char('/') + repositoryRelativePath);

    if (!isInsideRoot(libraryRoot, absolutePath)) {
        return QVariant::fromValue(nullptr);
    }

    return relativeTo(libraryRoot, absolutePath);
}

QVariantMap emptyGitCounts()
{
    return {
        {QStringLiteral("modified"), 0},
        {QStringLiteral("added"), 0},
        {QStringLiteral("deleted"), 0},
        {QStringLiteral("renamed"), 0},
        {QStringLiteral("conflicted"), 0},
        {QStringLiteral("untracked"), 0},
        {QStringLiteral("ignored"), 0},
    };
}
}

namespace LibraryFiles
{
QVariantMap readText(CoreDatabase &database, const QString &libraryId, const QString &fileId)
{
    const QVariantMap library = requireLibrary(database, libraryId);

    if (!isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Archived Libraries cannot preview files."));
    }

    const auto found = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!found) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    QVariantMap file = *found;
    const QString status = file.value(QStringLiteral("status")).toString();

    if (status != QStringLiteral("available")) {
        throw CoreError(QStringLiteral("This file is marked %1. Rescan the Library before opening it.").arg(status));
    }

    if (!LibraryFilePolicy::isSupportedTextFile(
            file.value(QStringLiteral("name")).toString(),
            file.value(QStringLiteral("extension")).toString()
        )) {
        throw CoreError(QStringLiteral("This file type is not supported for text preview."));
    }

    const QString rootPath = QFileInfo(library.value(QStringLiteral("rootPath")).toString()).canonicalFilePath();

    if (rootPath.isEmpty()) {
        throw CoreError(QStringLiteral("The Library folder could not be resolved."));
    }

    const QString absolutePath = joinPath(rootPath, file.value(QStringLiteral("relativePath")).toString());

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

    if (info.size() > LibraryFilePolicy::maxTextPreviewBytes) {
        throw CoreError(
            QStringLiteral("This file is too large to read. The current limit is %1 KB.")
                .arg(std::lround(LibraryFilePolicy::maxTextPreviewBytes / 1024.0)),
            {{QStringLiteral("sizeBytes"), info.size()}, {QStringLiteral("maximumBytes"), LibraryFilePolicy::maxTextPreviewBytes}}
        );
    }

    const QString canonicalFile = info.canonicalFilePath();

    if (canonicalFile.isEmpty()) {
        throw CoreError(QStringLiteral("The file could not be resolved."));
    }

    if (!isInsideRoot(rootPath, canonicalFile)) {
        throw CoreError(QStringLiteral("The requested file resolved outside the Library root."));
    }

    QFile handle(canonicalFile);

    if (!info.isReadable() || !handle.open(QIODevice::ReadOnly)) {
        throw CoreError(QStringLiteral("Archivist cannot read this file."));
    }

    const QByteArray bytes = handle.readAll();

    if (handle.error() != QFileDevice::NoError) {
        throw CoreError(QStringLiteral("The file could not be read."));
    }

    if (bytes.contains('\0')) {
        throw CoreError(QStringLiteral("This file appears to be binary."));
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString content = decoder(bytes);

    if (decoder.hasError()) {
        throw CoreError(QStringLiteral("This file is not valid UTF-8 text."));
    }

    qsizetype lineCount = 0;

    if (!content.isEmpty()) {
        lineCount = 1;

        for (qsizetype index = 0; index < content.size(); ++index) {
            const QChar character = content.at(index);

            if (character == QLatin1Char('\r')) {
                ++lineCount;

                if (index + 1 < content.size() && content.at(index + 1) == QLatin1Char('\n')) {
                    ++index;
                }
            } else if (character == QLatin1Char('\n')) {
                ++lineCount;
            }
        }
    }

    file.insert(QStringLiteral("sizeBytes"), info.size());
    file.insert(QStringLiteral("modifiedAt"), JsCompat::isoTimestamp(info.lastModified()));

    return {
        {QStringLiteral("file"), file},
        {QStringLiteral("content"), content},
        {QStringLiteral("encoding"), QStringLiteral("utf-8")},
        {QStringLiteral("lineCount"), lineCount},
        {QStringLiteral("readAt"), JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc())},
    };
}

QStringList directories(CoreDatabase &database, const QString &libraryId)
{
    const QVariantMap library = requireLibrary(database, libraryId);
    const QString rootPath = QFileInfo(library.value(QStringLiteral("rootPath")).toString()).canonicalFilePath();
    const QFileInfo root(rootPath);

    if (rootPath.isEmpty() || !root.isDir() || root.isSymbolicLink()) {
        throw CoreError(QStringLiteral("The Library folder could not be resolved."));
    }

    QStringList results;
    std::function<void(const QString &)> walk = [&](const QString &relativeDirectory) {
        const QString absoluteDirectory = joinPath(rootPath, relativeDirectory);

        if (!isInsideRoot(rootPath, absoluteDirectory)) {
            return;
        }

        QDirIterator iterator(absoluteDirectory, QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

        while (iterator.hasNext()) {
            iterator.next();
            const QFileInfo entry = iterator.fileInfo();
            const QString name = entry.fileName();

            if (!entry.isDir() || entry.isSymbolicLink()) {
                continue;
            }

            if (LibraryFilePolicy::isIgnoredCatalogDirectory(name) || LibraryFilePolicy::isManagedPhotoLibraryDirectory(name)
                || !LibraryFilePolicy::isCatalogableDirectory(name)) {
                continue;
            }

            const QString relativePath = joinRelative(relativeDirectory, name);
            results.append(relativePath);
            walk(relativePath);
        }
    };

    walk(QString());

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(results.begin(), results.end(), [&collator](const QString &left, const QString &right) {
        return collator.compare(left, right) < 0;
    });

    return results;
}

QVariantMap catalogWithDirectories(CoreDatabase &database, const QString &libraryId)
{
    requireLibrary(database, libraryId);
    QVariantMap result = LibraryFileRepository::catalog(database, libraryId);
    result.insert(QStringLiteral("directories"), toVariantList(directories(database, libraryId)));
    return result;
}

QVariantMap indexCatalog(CoreDatabase &database, const QString &libraryId, const QVariantList &files)
{
    QElapsedTimer timer;
    timer.start();

    QVariantList issues;
    int processed = 0;
    int unchanged = 0;
    int indexed = 0;
    int empty = 0;
    int unavailable = 0;
    int failed = 0;
    qint64 chunkCount = 0;

    auto reusable = [](const std::optional<QVariantMap> &document) {
        if (!document) {
            return false;
        }

        const QString status = document->value(QStringLiteral("status")).toString();
        return status == QStringLiteral("indexed") || status == QStringLiteral("empty");
    };

    for (const QVariant &value : files) {
        const QVariantMap file = value.toMap();
        const QString fileId = file.value(QStringLiteral("id")).toString();
        const QString fileLibraryId = file.value(QStringLiteral("libraryId")).toString();
        const QString status = file.value(QStringLiteral("status")).toString();
        const QString modifiedAt = file.value(QStringLiteral("modifiedAt")).toString();
        const qint64 sizeBytes = file.value(QStringLiteral("sizeBytes")).toLongLong();

        if (!LibraryFilePolicy::isSupportedTextFile(
                file.value(QStringLiteral("name")).toString(),
                file.value(QStringLiteral("extension")).toString()
            )) {
            continue;
        }

        ++processed;
        const auto existing = LibraryFileRepository::textDocument(database, fileId);

        if (status != QStringLiteral("available")) {
            database.transaction([&]() {
                LibraryFileRepository::markTextDocument(
                    database,
                    fileId,
                    fileLibraryId,
                    QStringLiteral("unavailable"),
                    modifiedAt,
                    sizeBytes,
                    QStringLiteral("The catalog marks this file as %1.").arg(status)
                );
            });
            ++unavailable;
            continue;
        }

        if (reusable(existing) && existing->value(QStringLiteral("sourceModifiedAt")).toString() == modifiedAt
            && existing->value(QStringLiteral("sourceSizeBytes")).toLongLong() == sizeBytes) {
            ++unchanged;
            chunkCount += existing->value(QStringLiteral("chunkCount")).toLongLong();
            continue;
        }

        try {
            const QVariantMap read = readText(database, fileLibraryId, fileId);
            const QVariantMap readFile = read.value(QStringLiteral("file")).toMap();
            const QString normalized = LibraryTextChunker::normalize(read.value(QStringLiteral("content")).toString());
            const QString contentHash = LibraryTextChunker::hash(normalized);
            const QString readModifiedAt = readFile.value(QStringLiteral("modifiedAt")).toString();
            const qint64 readSize = readFile.value(QStringLiteral("sizeBytes")).toLongLong();

            if (reusable(existing) && existing->value(QStringLiteral("contentHash")).toString() == contentHash) {
                LibraryFileRepository::touchTextDocument(database, fileId, readModifiedAt, readSize);
                ++unchanged;
                chunkCount += existing->value(QStringLiteral("chunkCount")).toLongLong();
                continue;
            }

            const auto chunks = LibraryTextChunker::chunk(
                fileLibraryId,
                fileId,
                file.value(QStringLiteral("extension")).toString(),
                normalized
            );

            database.transaction([&]() {
                LibraryFileRepository::replaceTextDocument(
                    database,
                    fileId,
                    fileLibraryId,
                    chunks.isEmpty() ? QStringLiteral("empty") : QStringLiteral("indexed"),
                    contentHash,
                    readModifiedAt,
                    readSize,
                    chunks
                );
            });

            chunkCount += chunks.size();
            chunks.isEmpty() ? ++empty : ++indexed;
        } catch (const std::exception &error) {
            const QString message = errorText(error);

            database.transaction([&]() {
                LibraryFileRepository::markTextDocument(
                    database,
                    fileId,
                    fileLibraryId,
                    QStringLiteral("failed"),
                    modifiedAt,
                    sizeBytes,
                    message
                );
            });

            ++failed;

            if (issues.size() < maxReportedIssues) {
                issues.append(QVariantMap{
                    {QStringLiteral("fileId"), fileId},
                    {QStringLiteral("relativePath"), file.value(QStringLiteral("relativePath"))},
                    {QStringLiteral("message"), message},
                });
            }
        }
    }

    const double durationMs = std::round(static_cast<double>(timer.nsecsElapsed()) / 1000.0) / 1000.0;

    return {
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("processedFileCount"), processed},
        {QStringLiteral("unchangedFileCount"), unchanged},
        {QStringLiteral("indexedFileCount"), indexed},
        {QStringLiteral("emptyFileCount"), empty},
        {QStringLiteral("unavailableFileCount"), unavailable},
        {QStringLiteral("failedFileCount"), failed},
        {QStringLiteral("chunkCount"), chunkCount},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("issues"), issues},
    };
}

QVariantMap scan(CoreDatabase &database, const QString &libraryId)
{
    const QVariantMap library = requireLibrary(database, libraryId);

    if (!isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Archived Libraries cannot be scanned."));
    }

    const QString rootPath = library.value(QStringLiteral("rootPath")).toString();
    const QVariantMap started = database.transaction([&]() { return LibraryFileRepository::createScan(database, libraryId); });
    const QString scanId = started.value(QStringLiteral("id")).toString();
    ScanAccumulator accumulator;
    QVariantMap completed;

    try {
        validateLibraryRoot(rootPath);
        scanDirectory(rootPath, QString(), accumulator);

        completed = database.transaction([&]() {
            return LibraryFileRepository::completeScan(
                database,
                scanId,
                libraryId,
                accumulator.files,
                accumulator.discoveredFileCount,
                accumulator.ignoredEntryCount,
                accumulator.errorCount
            );
        });
    } catch (const std::exception &error) {
        const QString message = errorText(error);
        qWarning().noquote() << "[Library scan]" << rootPath + QStringLiteral(":") << message;
        database.transaction([&]() { LibraryFileRepository::failScan(database, scanId, libraryId, message); });

        if (dynamic_cast<const CoreError *>(&error)) {
            throw;
        }

        throw CoreError(QStringLiteral("The Library scan failed."), {{QStringLiteral("cause"), message}});
    }

    QVariantMap result = LibraryFileRepository::catalog(database, libraryId);
    QVariantMap index;

    try {
        index = indexCatalog(database, libraryId, result.value(QStringLiteral("files")).toList());
    } catch (const std::exception &error) {
        const QString message = errorText(error);
        qWarning().noquote() << "[Library index]" << rootPath + QStringLiteral(":") << message;
        index = failedIndexSummary(libraryId, message);
    }

    result.insert(QStringLiteral("scan"), completed);
    result.insert(QStringLiteral("issues"), accumulator.issues);
    result.insert(QStringLiteral("index"), index);
    return result;
}

QVariantMap gitStatus(CoreDatabase &database, const QString &libraryId)
{
    const QVariantMap library = requireLibrary(database, libraryId);
    const QString libraryRoot = library.value(QStringLiteral("rootPath")).toString();

    const GitResult topLevel = runGit({QStringLiteral("-C"), libraryRoot, QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});

    if (!topLevel.started || topLevel.exitCode != 0) {
        if (topLevel.errors.contains(QStringLiteral("not a git repository"))
            || topLevel.errors.contains(QStringLiteral("not a git work tree"))) {
            return {
                {QStringLiteral("repository"), false},
                {QStringLiteral("repositoryRoot"), QVariant::fromValue(nullptr)},
                {QStringLiteral("branch"), QVariant::fromValue(nullptr)},
                {QStringLiteral("detached"), false},
                {QStringLiteral("upstream"), QVariant::fromValue(nullptr)},
                {QStringLiteral("ahead"), 0},
                {QStringLiteral("behind"), 0},
                {QStringLiteral("dirty"), false},
                {QStringLiteral("entries"), QVariantList{}},
                {QStringLiteral("counts"), emptyGitCounts()},
            };
        }

        throw CoreError(
            QStringLiteral("Archivist could not inspect Git status."),
            {{QStringLiteral("cause"), topLevel.errors.trimmed()}}
        );
    }

    const QString repositoryRoot = topLevel.output.trimmed();
    const GitResult status = runGit({
        QStringLiteral("-C"),
        libraryRoot,
        QStringLiteral("status"),
        QStringLiteral("--porcelain=v2"),
        QStringLiteral("-z"),
        QStringLiteral("--branch"),
        QStringLiteral("--untracked-files=all"),
        QStringLiteral("--ignored=matching"),
    });

    if (!status.started || status.exitCode != 0) {
        throw CoreError(
            QStringLiteral("Archivist could not read Git status."),
            {{QStringLiteral("cause"), status.errors.trimmed()}}
        );
    }

    static const QRegularExpression tracked(
        QStringLiteral("^1 ([^ ]{2}) (?:[^ ]+ ){6}(.+)$"),
        QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression unmerged(
        QStringLiteral("^u ([^ ]{2}) (?:[^ ]+ ){8}(.+)$"),
        QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression renamed(
        QStringLiteral("^2 ([^ ]{2}) (?:[^ ]+ ){7}(.+)$"),
        QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression aheadBehind(QStringLiteral("^\\+(\\d+)\\s+-(\\d+)$"));

    const QStringList tokens = status.output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    QList<QVariantMap> entries;
    QVariantMap counts = emptyGitCounts();
    QVariant branch = QVariant::fromValue(nullptr);
    bool detached = false;
    QVariant upstream = QVariant::fromValue(nullptr);
    int ahead = 0;
    int behind = 0;

    for (qsizetype index = 0; index < tokens.size(); ++index) {
        const QString &token = tokens.at(index);

        if (token.startsWith(QStringLiteral("# branch.head "))) {
            const QString value = token.mid(14).trimmed();
            detached = value == QStringLiteral("(detached)");
            branch = detached ? QVariant::fromValue(nullptr) : QVariant(value);
            continue;
        }

        if (token.startsWith(QStringLiteral("# branch.upstream "))) {
            const QString value = token.mid(18).trimmed();
            upstream = value.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(value);
            continue;
        }

        if (token.startsWith(QStringLiteral("# branch.ab "))) {
            const auto match = aheadBehind.match(token.mid(12).trimmed());
            ahead = match.hasMatch() ? match.captured(1).toInt() : 0;
            behind = match.hasMatch() ? match.captured(2).toInt() : 0;
            continue;
        }

        if (token.startsWith(QStringLiteral("# "))) {
            continue;
        }

        const QChar recordType = token.at(0);
        QString indexStatus;
        QString worktreeStatus;
        QString path;
        QString originalPath;

        auto apply = [&](const QRegularExpression &pattern) {
            const auto match = pattern.match(token);

            if (!match.hasMatch()) {
                return false;
            }

            indexStatus = match.captured(1).left(1);
            worktreeStatus = match.captured(1).mid(1, 1);
            path = match.captured(2);
            return true;
        };

        if (recordType == QLatin1Char('1')) {
            if (!apply(tracked)) {
                continue;
            }
        } else if (recordType == QLatin1Char('u')) {
            if (!apply(unmerged)) {
                continue;
            }
        } else if (recordType == QLatin1Char('2')) {
            const bool matched = apply(renamed);
            originalPath = index + 1 < tokens.size() ? tokens.at(index + 1) : QString();
            ++index;

            if (!matched) {
                continue;
            }
        } else if (recordType == QLatin1Char('?') || recordType == QLatin1Char('!')) {
            indexStatus = recordType;
            worktreeStatus = recordType;
            path = token.mid(2);
        } else {
            continue;
        }

        if (path.isEmpty()) {
            continue;
        }

        const QVariant libraryPath = gitLibraryPath(repositoryRoot, libraryRoot, path);

        if (isNullish(libraryPath)) {
            continue;
        }

        const QString classified = classifyGitStatus(recordType, indexStatus, worktreeStatus);
        entries.append({
            {QStringLiteral("path"), libraryPath},
            {QStringLiteral("originalPath"),
             originalPath.isEmpty() ? QVariant::fromValue(nullptr) : gitLibraryPath(repositoryRoot, libraryRoot, originalPath)},
            {QStringLiteral("status"), classified},
            {QStringLiteral("indexStatus"), indexStatus},
            {QStringLiteral("worktreeStatus"), worktreeStatus},
            {QStringLiteral("directory"), path.endsWith(QLatin1Char('/'))},
        });
        counts.insert(classified, counts.value(classified).toInt() + 1);
    }

    QCollator collator;
    std::stable_sort(entries.begin(), entries.end(), [&collator](const QVariantMap &left, const QVariantMap &right) {
        return collator.compare(left.value(QStringLiteral("path")).toString(), right.value(QStringLiteral("path")).toString()) < 0;
    });

    QVariantList entryList;
    bool dirty = false;

    for (const QVariantMap &entry : entries) {
        dirty = dirty || entry.value(QStringLiteral("status")).toString() != QStringLiteral("ignored");
        entryList.append(entry);
    }

    return {
        {QStringLiteral("repository"), true},
        {QStringLiteral("repositoryRoot"), repositoryRoot},
        {QStringLiteral("branch"), branch},
        {QStringLiteral("detached"), detached},
        {QStringLiteral("upstream"), upstream},
        {QStringLiteral("ahead"), ahead},
        {QStringLiteral("behind"), behind},
        {QStringLiteral("dirty"), dirty},
        {QStringLiteral("entries"), entryList},
        {QStringLiteral("counts"), counts},
    };
}
}
