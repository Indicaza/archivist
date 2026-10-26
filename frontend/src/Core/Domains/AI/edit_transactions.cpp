#include "edit_transactions.h"

#include "Core/Domains/Libraries/library_edit_inspection.h"
#include "Core/Domains/Libraries/library_file_policy.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"
#include "edit_proposals.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace CoreValues;
namespace Inspection = LibraryEditInspection;

namespace
{
using Rollback = std::function<void()>;

// AIEditProposalStaleError
struct StaleError
{
    QString message;
};

// A failed filesystem call, shaped like a Node ErrnoException message.
class FsError final : public std::runtime_error
{
public:
    FsError(int number, const QString &syscall, const QString &path, const QString &destination = {})
        : std::runtime_error(std::string())
        , m_code(codeName(number))
    {
        m_message = QStringLiteral("%1: %2, %3 '%4'").arg(m_code, description(number), syscall, path);

        if (!destination.isEmpty()) {
            m_message += QStringLiteral(" -> '%1'").arg(destination);
        }
    }

    [[nodiscard]] const char *what() const noexcept override
    {
        m_utf8 = m_message.toUtf8();
        return m_utf8.constData();
    }

    [[nodiscard]] const QString &code() const noexcept { return m_code; }
    [[nodiscard]] const QString &message() const noexcept { return m_message; }

private:
    static QString codeName(int number)
    {
        switch (number) {
        case EEXIST: return QStringLiteral("EEXIST");
        case ENOENT: return QStringLiteral("ENOENT");
        case ENOTDIR: return QStringLiteral("ENOTDIR");
        case ELOOP: return QStringLiteral("ELOOP");
        case EACCES: return QStringLiteral("EACCES");
        case EPERM: return QStringLiteral("EPERM");
        case ENOTEMPTY: return QStringLiteral("ENOTEMPTY");
        case EISDIR: return QStringLiteral("EISDIR");
        case EXDEV: return QStringLiteral("EXDEV");
        case ENOSPC: return QStringLiteral("ENOSPC");
        case EIO: return QStringLiteral("EIO");
        default: return QStringLiteral("E%1").arg(number);
        }
    }

    static QString description(int number)
    {
        switch (number) {
        case EEXIST: return QStringLiteral("file already exists");
        case ENOENT: return QStringLiteral("no such file or directory");
        case ENOTDIR: return QStringLiteral("not a directory");
        case ELOOP: return QStringLiteral("too many symbolic links encountered");
        case EACCES: return QStringLiteral("permission denied");
        case EPERM: return QStringLiteral("operation not permitted");
        case ENOTEMPTY: return QStringLiteral("directory not empty");
        case EISDIR: return QStringLiteral("illegal operation on a directory");
        case EXDEV: return QStringLiteral("cross-device link not permitted");
        case ENOSPC: return QStringLiteral("no space left on device");
        case EIO: return QStringLiteral("i/o error");
        default: return QString::fromLocal8Bit(std::strerror(number)).toLower();
        }
    }

    QString m_code;
    QString m_message;
    mutable QByteArray m_utf8;
};

QByteArray native(const QString &path)
{
    return QFile::encodeName(path);
}

void check(int result, const QString &syscall, const QString &path, const QString &destination = {})
{
    if (result != 0) {
        throw FsError(errno, syscall, path, destination);
    }
}

void fsLink(const QString &from, const QString &to)
{
    check(::link(native(from).constData(), native(to).constData()), QStringLiteral("link"), from, to);
}

void fsUnlink(const QString &path)
{
    check(::unlink(native(path).constData()), QStringLiteral("unlink"), path);
}

void fsRename(const QString &from, const QString &to)
{
    check(::rename(native(from).constData(), native(to).constData()), QStringLiteral("rename"), from, to);
}

void fsMkdir(const QString &path, mode_t mode = 0777)
{
    check(::mkdir(native(path).constData(), mode), QStringLiteral("mkdir"), path);
}

void fsRmdir(const QString &path)
{
    check(::rmdir(native(path).constData()), QStringLiteral("rmdir"), path);
}

mode_t fsMode(const QString &path)
{
    struct stat info {};
    check(::lstat(native(path).constData(), &info), QStringLiteral("lstat"), path);
    return info.st_mode & 07777;
}

// writeFile(path, content, {flag: "wx", mode})
void writeExclusive(const QString &path, const QString &content, mode_t mode = 0666)
{
    const int descriptor = ::open(native(path).constData(), O_WRONLY | O_CREAT | O_EXCL, mode);

    if (descriptor < 0) {
        throw FsError(errno, QStringLiteral("open"), path);
    }

    const QByteArray bytes = content.toUtf8();
    qsizetype written = 0;

    while (written < bytes.size()) {
        const ssize_t result = ::write(descriptor, bytes.constData() + written, static_cast<size_t>(bytes.size() - written));

        if (result < 0) {
            const int number = errno;
            ::close(descriptor);
            throw FsError(number, QStringLiteral("write"), path);
        }

        written += result;
    }

    ::close(descriptor);
}

void ignoreErrors(const std::function<void()> &work)
{
    try {
        work();
    } catch (...) {
    }
}

QString errorMessage(const std::exception &error)
{
    QString message;

    if (const auto *core = dynamic_cast<const CoreError *>(&error)) {
        message = core->message();
    } else if (const auto *fs = dynamic_cast<const FsError *>(&error)) {
        message = fs->message();
    } else {
        message = QString::fromUtf8(error.what());
    }

    return JsCompat::isBlank(message) ? QStringLiteral("An unknown AI edit transaction error occurred.") : message;
}

bool isStaleFilesystemError(const FsError &error)
{
    static const QSet<QString> codes{
        QStringLiteral("EEXIST"), QStringLiteral("ENOENT"), QStringLiteral("ENOTDIR"), QStringLiteral("ELOOP"),
        QStringLiteral("EACCES"), QStringLiteral("EPERM"), QStringLiteral("ENOTEMPTY"),
    };
    return codes.contains(error.code());
}

QString canonicalRoot(CoreDatabase &database, const QString &libraryId)
{
    try {
        return Inspection::canonicalLibraryRoot(database, libraryId);
    } catch (const CoreError &error) {
        if (error.message() == QStringLiteral("Archived Libraries cannot accept AI edit proposals.")) {
            throw CoreError(QStringLiteral("Archived Libraries cannot execute AI edit proposals."));
        }

        throw;
    }
}

QString absolutePath(const QString &rootPath, const QString &relativePath)
{
    const QString path = QDir::cleanPath(rootPath + QLatin1Char('/') + relativePath);

    if (!Inspection::isInsideRoot(rootPath, path)) {
        throw CoreError(QStringLiteral("The AI edit operation escaped the Library root."));
    }

    return path;
}

QString parentDirectory(const QString &path)
{
    return path.left(path.lastIndexOf(QLatin1Char('/')));
}

QString fileName(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

QString text(const QVariantMap &operation, const char *key)
{
    const QVariant value = operation.value(QLatin1String(key));
    return isNullish(value) ? QString() : value.toString();
}

bool hasText(const QVariantMap &operation, const char *key)
{
    return !isNullish(operation.value(QLatin1String(key)));
}

QString requireField(const QVariantMap &operation, const char *key, const QString &label)
{
    const QString value = text(operation, key);

    if (value.isEmpty() && (std::strcmp(key, "afterContent") != 0 && std::strcmp(key, "beforeContent") != 0)) {
        throw CoreError(QStringLiteral("AI edit operation %1 is missing its %2.").arg(operation.value(QStringLiteral("id")).toString(), label));
    }

    if (!hasText(operation, key)) {
        throw CoreError(QStringLiteral("AI edit operation %1 is missing its %2.").arg(operation.value(QStringLiteral("id")).toString(), label));
    }

    return value;
}

QString sourcePath(const QVariantMap &operation) { return requireField(operation, "sourcePath", QStringLiteral("source path")); }
QString destinationPath(const QVariantMap &operation) { return requireField(operation, "destinationPath", QStringLiteral("destination path")); }
QString expectedHash(const QVariantMap &operation) { return requireField(operation, "expectedHash", QStringLiteral("expected hash")); }
QString afterHash(const QVariantMap &operation) { return requireField(operation, "afterHash", QStringLiteral("final hash")); }
QString afterContent(const QVariantMap &operation) { return requireField(operation, "afterContent", QStringLiteral("proposed content")); }
QString beforeContent(const QVariantMap &operation) { return requireField(operation, "beforeContent", QStringLiteral("original content")); }

void requireWritablePath(const QString &path)
{
    if (::access(native(path).constData(), W_OK) != 0) {
        throw CoreError(QStringLiteral("Archivist cannot write the approved edit source."));
    }
}

void requireWritableParent(const QString &path)
{
    if (::access(native(parentDirectory(path)).constData(), W_OK) != 0) {
        throw CoreError(QStringLiteral("Archivist cannot modify the approved edit source folder."));
    }
}

QVariantList operationsWithStatus(const QVariantMap &proposal, const QString &status, bool descending = false)
{
    QVariantList result;

    for (const QVariant &operation : proposal.value(QStringLiteral("operations")).toList()) {
        if (operation.toMap().value(QStringLiteral("status")).toString() == status) {
            result.append(operation);
        }
    }

    std::stable_sort(result.begin(), result.end(), [descending](const QVariant &left, const QVariant &right) {
        const int a = left.toMap().value(QStringLiteral("ordinal")).toInt();
        const int b = right.toMap().value(QStringLiteral("ordinal")).toInt();
        return descending ? a > b : a < b;
    });
    return result;
}

QVariantList ids(const QVariantList &operations)
{
    QVariantList result;

    for (const QVariant &operation : operations) {
        result.append(operation.toMap().value(QStringLiteral("id")));
    }

    return result;
}

QString hashFile(const QString &path)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        throw FsError(ENOENT, QStringLiteral("open"), path);
    }

    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

// Runs an inspection and turns any failure into a StaleError.
template<typename Work>
void staleOnFailure(Work &&work)
{
    try {
        work();
    } catch (const StaleError &) {
        throw;
    } catch (const std::exception &error) {
        throw StaleError{errorMessage(error)};
    }
}

// ---- forward execution ----

void inspectOperation(CoreDatabase &database, const QVariantMap &proposal, const QVariantMap &operation)
{
    const QString libraryId = proposal.value(QStringLiteral("libraryId")).toString();
    const QString type = operation.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("create_file")) {
        (void)Inspection::createTarget(database, libraryId, destinationPath(operation));
        return;
    }

    if (type == QStringLiteral("create_directory")) {
        (void)Inspection::directoryCreateTarget(database, libraryId, destinationPath(operation));
        return;
    }

    if (type == QStringLiteral("patch_file")) {
        const QString source = sourcePath(operation);
        const QString expected = expectedHash(operation);
        const auto inspection = Inspection::textFile(database, libraryId, source);

        if (inspection.sha256 != expected || !hasText(operation, "beforeContent")
            || inspection.content != text(operation, "beforeContent")) {
            throw StaleError{QStringLiteral("The approved patch source \"%1\" changed after proposal review.").arg(source)};
        }

        requireWritablePath(absolutePath(canonicalRoot(database, libraryId), source));
        return;
    }

    if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
        const QString source = sourcePath(operation);
        const QString destination = destinationPath(operation);
        const QString expected = expectedHash(operation);
        const auto inspection = Inspection::textFile(database, libraryId, source);

        if (inspection.sha256 != expected) {
            throw StaleError{QStringLiteral("The approved %1 source \"%2\" changed after proposal review.").arg(type, source)};
        }

        (void)Inspection::createTarget(database, libraryId, destination);
        requireWritableParent(absolutePath(canonicalRoot(database, libraryId), source));
        return;
    }

    throw CoreError(QStringLiteral("Unsupported AI edit operation type \"%1\".").arg(type));
}

struct Executed
{
    Rollback rollback;
    QString absolutePath; // empty for directories
};

Executed executeOperation(const QString &rootPath, const QString &proposalId, const QVariantMap &operation)
{
    const QString type = operation.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("create_file")) {
        const QString destination = absolutePath(rootPath, destinationPath(operation));
        writeExclusive(destination, afterContent(operation));
        return {[destination]() { fsUnlink(destination); }, destination};
    }

    if (type == QStringLiteral("create_directory")) {
        const QString destination = absolutePath(rootPath, destinationPath(operation));
        fsMkdir(destination);
        return {[destination]() { fsRmdir(destination); }, QString()};
    }

    if (type == QStringLiteral("patch_file")) {
        const QString source = absolutePath(rootPath, sourcePath(operation));
        const mode_t mode = fsMode(source);
        const QString temporary = parentDirectory(source)
            + QStringLiteral("/.%1.archivist-%2-%3.tmp").arg(fileName(source), proposalId, newId());

        try {
            writeExclusive(temporary, afterContent(operation), mode);
            fsRename(temporary, source);
        } catch (...) {
            ignoreErrors([&]() { fsUnlink(temporary); });
            throw;
        }

        const QString operationId = operation.value(QStringLiteral("id")).toString();
        const bool hasBefore = hasText(operation, "beforeContent");
        const QString before = text(operation, "beforeContent");

        return {
            [source, mode, operationId, hasBefore, before]() {
                if (!hasBefore) {
                    throw CoreError(QStringLiteral("AI edit operation %1 has no rollback content.").arg(operationId));
                }

                const QString rollbackPath =
                    parentDirectory(source) + QStringLiteral("/.%1.archivist-rollback-%2.tmp").arg(fileName(source), newId());

                try {
                    writeExclusive(rollbackPath, before, mode);
                    fsRename(rollbackPath, source);
                } catch (...) {
                    ignoreErrors([&]() { fsUnlink(rollbackPath); });
                    throw;
                }
            },
            source,
        };
    }

    if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
        const QString source = absolutePath(rootPath, sourcePath(operation));
        const QString destination = absolutePath(rootPath, destinationPath(operation));
        fsLink(source, destination);

        try {
            fsUnlink(source);
        } catch (...) {
            ignoreErrors([&]() { fsUnlink(destination); });
            throw;
        }

        return {
            [source, destination]() {
                fsLink(destination, source);

                try {
                    fsUnlink(destination);
                } catch (...) {
                    ignoreErrors([&]() { fsUnlink(source); });
                    throw;
                }
            },
            destination,
        };
    }

    throw CoreError(QStringLiteral("Unsupported AI edit operation type \"%1\".").arg(type));
}

QStringList runRollbacks(const QList<Rollback> &rollbacks)
{
    QStringList errors;

    for (auto iterator = rollbacks.crbegin(); iterator != rollbacks.crend(); ++iterator) {
        try {
            (*iterator)();
        } catch (const std::exception &error) {
            errors.append(errorMessage(error));
        }
    }

    return errors;
}

// ---- undo ----

void inspectUndoOperation(CoreDatabase &database, const QVariantMap &proposal, const QVariantMap &operation)
{
    const QString libraryId = proposal.value(QStringLiteral("libraryId")).toString();
    const QString type = operation.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("create_file") || type == QStringLiteral("patch_file")) {
        const bool created = type == QStringLiteral("create_file");
        const QString path = created ? destinationPath(operation) : sourcePath(operation);
        const auto inspection = Inspection::textFile(database, libraryId, path);

        if (inspection.sha256 != afterHash(operation) || inspection.content != afterContent(operation)) {
            throw StaleError{
                (created ? QStringLiteral("The created file \"%1\" changed after Archivist wrote it.")
                         : QStringLiteral("The patched file \"%1\" changed after Archivist wrote it."))
                    .arg(path),
            };
        }

        requireWritableParent(absolutePath(canonicalRoot(database, libraryId), path));
        return;
    }

    if (type == QStringLiteral("create_directory")) {
        (void)Inspection::emptyDirectory(database, libraryId, destinationPath(operation));
        return;
    }

    if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
        const QString source = sourcePath(operation);
        const QString destination = destinationPath(operation);
        const auto inspection = Inspection::textFile(database, libraryId, destination);

        if (inspection.sha256 != afterHash(operation)) {
            throw StaleError{QStringLiteral("The relocated file \"%1\" changed after Archivist wrote it.").arg(destination)};
        }

        (void)Inspection::createTarget(database, libraryId, source);
        requireWritableParent(absolutePath(canonicalRoot(database, libraryId), destination));
        return;
    }

    throw CoreError(QStringLiteral("Unsupported AI edit operation type \"%1\".").arg(type));
}

Rollback executeUndoOperation(const QString &rootPath, const QString &proposalId, const QVariantMap &operation)
{
    const QString type = operation.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("create_file")) {
        const QString destination = absolutePath(rootPath, destinationPath(operation));
        const mode_t mode = fsMode(destination);
        const QString content = afterContent(operation);
        fsUnlink(destination);
        return [destination, content, mode]() { writeExclusive(destination, content, mode); };
    }

    if (type == QStringLiteral("create_directory")) {
        const QString destination = absolutePath(rootPath, destinationPath(operation));
        const mode_t mode = fsMode(destination);
        fsRmdir(destination);
        return [destination, mode]() { fsMkdir(destination, mode); };
    }

    if (type == QStringLiteral("patch_file")) {
        const QString source = absolutePath(rootPath, sourcePath(operation));
        const mode_t mode = fsMode(source);
        const QString before = beforeContent(operation);
        const QString after = afterContent(operation);
        const QString temporary = parentDirectory(source)
            + QStringLiteral("/.%1.archivist-undo-%2-%3.tmp").arg(fileName(source), proposalId, newId());

        try {
            writeExclusive(temporary, before, mode);
            fsRename(temporary, source);
        } catch (...) {
            ignoreErrors([&]() { fsUnlink(temporary); });
            throw;
        }

        return [source, mode, after]() {
            const QString rollbackPath =
                parentDirectory(source) + QStringLiteral("/.%1.archivist-undo-rollback-%2.tmp").arg(fileName(source), newId());

            try {
                writeExclusive(rollbackPath, after, mode);
                fsRename(rollbackPath, source);
            } catch (...) {
                ignoreErrors([&]() { fsUnlink(rollbackPath); });
                throw;
            }
        };
    }

    if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
        const QString restored = absolutePath(rootPath, sourcePath(operation));
        const QString current = absolutePath(rootPath, destinationPath(operation));
        fsLink(current, restored);

        try {
            fsUnlink(current);
        } catch (...) {
            ignoreErrors([&]() { fsUnlink(restored); });
            throw;
        }

        return [restored, current]() {
            fsLink(restored, current);

            try {
                fsUnlink(restored);
            } catch (...) {
                ignoreErrors([&]() { fsUnlink(current); });
                throw;
            }
        };
    }

    throw CoreError(QStringLiteral("Unsupported AI edit operation type \"%1\".").arg(type));
}

// ---- synchronization ----

QString message(const std::exception &error)
{
    QString text = errorMessage(error);
    return text == QStringLiteral("An unknown AI edit transaction error occurred.")
        ? QStringLiteral("An unknown post-edit synchronization error occurred.")
        : text;
}

QVariantMap warning(const QString &stage, const QString &text)
{
    return {{QStringLiteral("stage"), stage}, {QStringLiteral("message"), text}};
}

QVariantMap synchronize(CoreDatabase &database, const QVariantMap &proposal, bool forward)
{
    const QString expected = forward ? QStringLiteral("completed") : QStringLiteral("undone");
    const QString proposalId = proposal.value(QStringLiteral("id")).toString();
    const QString libraryId = proposal.value(QStringLiteral("libraryId")).toString();

    if (proposal.value(QStringLiteral("status")).toString() != expected) {
        throw CoreError(QStringLiteral("AI edit proposal %1 must be %2 before synchronization.").arg(proposalId, expected));
    }

    const QVariantList operations = operationsWithStatus(proposal, expected);
    QVariantList warnings;
    const QStringList paths = AIEditProposals::affectedPaths(operations);
    const QSet<QString> pathSet(paths.cbegin(), paths.cend());
    QVariantList relocations;

    for (const QVariant &value : operations) {
        const QVariantMap operation = value.toMap();
        const QString type = operation.value(QStringLiteral("type")).toString();

        if ((type != QStringLiteral("rename_file") && type != QStringLiteral("move_file")) || !hasText(operation, "sourcePath")
            || !hasText(operation, "destinationPath")) {
            continue;
        }

        const QString current = forward ? text(operation, "sourcePath") : text(operation, "destinationPath");
        const QString target = forward ? text(operation, "destinationPath") : text(operation, "sourcePath");
        const auto file = LibraryFileRepository::fileByRelativePath(database, libraryId, current);

        if (!file) {
            continue;
        }

        try {
            const QString targetExtension = LibraryFilePolicy::extensionOf(fileName(target));
            const QVariantMap updated = LibraryFileRepository::updateFileLocation(
                database, libraryId, file->value(QStringLiteral("id")).toString(), target, fileName(target), targetExtension
            );

            if (file->value(QStringLiteral("extension")).toString() != targetExtension) {
                LibraryFileRepository::markTextDocument(
                    database,
                    file->value(QStringLiteral("id")).toString(),
                    libraryId,
                    QStringLiteral("unavailable"),
                    file->value(QStringLiteral("modifiedAt")).toString(),
                    file->value(QStringLiteral("sizeBytes")).toLongLong(),
                    QStringLiteral("Reindex required after file extension change.")
                );
            }

            relocations.append(QVariantMap{
                {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
                {QStringLiteral("fileId"), updated.value(QStringLiteral("id"))},
                {QStringLiteral("sourcePath"), current},
                {QStringLiteral("destinationPath"), target},
            });
        } catch (const std::exception &error) {
            warnings.append(warning(
                QStringLiteral("catalog-identity"),
                QStringLiteral("Could not preserve catalog identity for %1 → %2: %3").arg(current, target, message(error))
            ));
        }
    }

    QVariant catalog = QVariant::fromValue(nullptr);
    QVariant index = QVariant::fromValue(nullptr);

    try {
        const QVariantMap scanned = LibraryFiles::scan(database, libraryId);
        const QVariantMap scan = scanned.value(QStringLiteral("scan")).toMap();
        const QVariantList files = scanned.value(QStringLiteral("files")).toList();
        const QVariantMap indexResult = scanned.value(QStringLiteral("index")).toMap();
        const QVariantList issues = scanned.value(QStringLiteral("issues")).toList();
        const QVariantList indexIssues = indexResult.value(QStringLiteral("issues")).toList();
        QVariantList affectedFiles;

        for (const QVariant &value : files) {
            const QVariantMap file = value.toMap();

            if (pathSet.contains(file.value(QStringLiteral("relativePath")).toString())) {
                affectedFiles.append(QVariantMap{
                    {QStringLiteral("id"), file.value(QStringLiteral("id"))},
                    {QStringLiteral("relativePath"), file.value(QStringLiteral("relativePath"))},
                    {QStringLiteral("status"), file.value(QStringLiteral("status"))},
                    {QStringLiteral("sizeBytes"), file.value(QStringLiteral("sizeBytes"))},
                    {QStringLiteral("modifiedAt"), file.value(QStringLiteral("modifiedAt"))},
                });
            }
        }

        catalog = QVariantMap{
            {QStringLiteral("scanId"), scan.value(QStringLiteral("id"))},
            {QStringLiteral("scanStatus"), scan.value(QStringLiteral("status"))},
            {QStringLiteral("catalogFileCount"), files.size()},
            {QStringLiteral("affectedFiles"), affectedFiles},
        };

        QVariantMap indexSummary;

        for (const char *key : {"processedFileCount", "unchangedFileCount", "indexedFileCount", "emptyFileCount",
                                "unavailableFileCount", "failedFileCount", "chunkCount"}) {
            indexSummary.insert(QLatin1String(key), indexResult.value(QLatin1String(key)));
        }

        indexSummary.insert(QStringLiteral("issueCount"), indexIssues.size());
        index = indexSummary;

        if (scan.value(QStringLiteral("status")).toString() != QStringLiteral("complete") || !issues.isEmpty()) {
            warnings.append(warning(
                QStringLiteral("catalog-index"),
                QStringLiteral("Library catalog refresh completed with %1 reported scan issue(s).").arg(issues.size())
            ));
        }

        const int failed = indexResult.value(QStringLiteral("failedFileCount")).toInt();

        if (failed > 0 || !indexIssues.isEmpty()) {
            warnings.append(warning(
                QStringLiteral("catalog-index"),
                QStringLiteral("Library text indexing completed with %1 failed file(s) and %2 reported issue(s).")
                    .arg(failed)
                    .arg(indexIssues.size())
            ));
        }
    } catch (const std::exception &error) {
        warnings.append(warning(QStringLiteral("catalog-index"), message(error)));
    }

    QVariant directories = QVariant::fromValue(nullptr);

    try {
        const QStringList directoryPaths = LibraryFiles::directories(database, libraryId);
        QSet<QString> relevant;

        for (const QString &path : paths) {
            const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));

            if (slash > 0) {
                relevant.insert(path.left(slash));
            }
        }

        for (const QVariant &value : operations) {
            const QVariantMap operation = value.toMap();

            if (operation.value(QStringLiteral("type")).toString() == QStringLiteral("create_directory") && hasText(operation, "destinationPath")) {
                relevant.insert(text(operation, "destinationPath"));
            }
        }

        QVariantList affected;

        for (const QString &path : directoryPaths) {
            if (relevant.contains(path)) {
                affected.append(path);
            }
        }

        directories = QVariantMap{{QStringLiteral("count"), directoryPaths.size()}, {QStringLiteral("affectedDirectories"), affected}};
    } catch (const std::exception &error) {
        warnings.append(warning(QStringLiteral("directories"), message(error)));
    }

    QVariant gitStatus = QVariant::fromValue(nullptr);

    try {
        gitStatus = LibraryFiles::gitStatus(database, libraryId);
    } catch (const std::exception &error) {
        warnings.append(warning(QStringLiteral("git-status"), message(error)));
    }

    return {
        {QStringLiteral("status"), warnings.isEmpty() ? QStringLiteral("synchronized") : QStringLiteral("warning")},
        {QStringLiteral("affectedPaths"), toVariantList(paths)},
        {QStringLiteral("relocations"), relocations},
        {QStringLiteral("catalog"), catalog},
        {QStringLiteral("directories"), directories},
        {QStringLiteral("index"), index},
        {QStringLiteral("gitStatus"), gitStatus},
        {QStringLiteral("warnings"), warnings},
    };
}

QVariantMap synchronizationPayload(const QVariantMap &result)
{
    QVariant catalog = QVariant::fromValue(nullptr);
    QVariant git = QVariant::fromValue(nullptr);

    if (result.value(QStringLiteral("catalog")).typeId() == QMetaType::QVariantMap) {
        const QVariantMap value = result.value(QStringLiteral("catalog")).toMap();
        catalog = QVariantMap{
            {QStringLiteral("scanId"), value.value(QStringLiteral("scanId"))},
            {QStringLiteral("scanStatus"), value.value(QStringLiteral("scanStatus"))},
            {QStringLiteral("catalogFileCount"), value.value(QStringLiteral("catalogFileCount"))},
            {QStringLiteral("affectedFileCount"), value.value(QStringLiteral("affectedFiles")).toList().size()},
        };
    }

    if (result.value(QStringLiteral("gitStatus")).typeId() == QMetaType::QVariantMap) {
        const QVariantMap value = result.value(QStringLiteral("gitStatus")).toMap();
        git = QVariantMap{
            {QStringLiteral("repository"), value.value(QStringLiteral("repository"))},
            {QStringLiteral("branch"), value.value(QStringLiteral("branch"))},
            {QStringLiteral("dirty"), value.value(QStringLiteral("dirty"))},
            {QStringLiteral("entryCount"), value.value(QStringLiteral("entries")).toList().size()},
            {QStringLiteral("counts"), value.value(QStringLiteral("counts"))},
        };
    }

    return {
        {QStringLiteral("affectedPaths"), result.value(QStringLiteral("affectedPaths"))},
        {QStringLiteral("relocations"), result.value(QStringLiteral("relocations"))},
        {QStringLiteral("catalog"), catalog},
        {QStringLiteral("directories"), result.value(QStringLiteral("directories"))},
        {QStringLiteral("index"), result.value(QStringLiteral("index"))},
        {QStringLiteral("gitStatus"), git},
        {QStringLiteral("warningCount"), result.value(QStringLiteral("warnings")).toList().size()},
        {QStringLiteral("warnings"), result.value(QStringLiteral("warnings"))},
    };
}

QVariantList uniquePaths(const QVariantList &changes)
{
    QVariantList result;

    for (const QVariant &change : changes) {
        for (const char *key : {"sourcePath", "destinationPath"}) {
            const QVariant value = change.toMap().value(QLatin1String(key));

            if (value.typeId() == QMetaType::QString && !result.contains(value)) {
                result.append(value);
            }
        }
    }

    return result;
}
}

namespace AIEditTransactions
{
QVariantMap reject(CoreDatabase &database, const QString &proposalId, const EventSink &sink)
{
    const QVariantMap rejected = AIEditProposals::reject(database, proposalId);
    sink(rejected.value(QStringLiteral("runId")).toString(), QStringLiteral("edit.rejected"), {
        {QStringLiteral("proposalId"), rejected.value(QStringLiteral("id"))},
        {QStringLiteral("operationIds"), ids(rejected.value(QStringLiteral("operations")).toList())},
    });
    return rejected;
}

QVariantMap approve(CoreDatabase &database, const QString &proposalId, const std::optional<QStringList> &selected, const EventSink &sink)
{
    const QVariantMap executing = AIEditProposals::beginExecution(database, proposalId, selected);
    const QString runId = executing.value(QStringLiteral("runId")).toString();
    const QString libraryId = executing.value(QStringLiteral("libraryId")).toString();
    const QVariantList approved = operationsWithStatus(executing, QStringLiteral("approved"));
    const QVariantList approvedIds = ids(approved);
    const QVariantList rejectedIds = ids(operationsWithStatus(executing, QStringLiteral("rejected")));

    sink(runId, QStringLiteral("edit.approved"), {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("approvedOperationIds"), approvedIds},
        {QStringLiteral("rejectedOperationIds"), rejectedIds},
        {QStringLiteral("partial"), !rejectedIds.isEmpty()},
    });

    QString rootPath;

    try {
        if (approved.isEmpty()) {
            throw CoreError(QStringLiteral("This AI edit proposal has no approved operations."));
        }

        for (const QVariant &operation : approved) {
            staleOnFailure([&]() { inspectOperation(database, executing, operation.toMap()); });
        }

        rootPath = canonicalRoot(database, libraryId);
    } catch (const StaleError &error) {
        AIEditProposals::markStale(database, proposalId, error.message);
        sink(runId, QStringLiteral("edit.stale"), {
            {QStringLiteral("proposalId"), proposalId},
            {QStringLiteral("approvedOperationIds"), approvedIds},
            {QStringLiteral("message"), error.message},
            {QStringLiteral("writesPerformed"), false},
            {QStringLiteral("writesRetained"), false},
        });
        throw CoreError(
            QStringLiteral("The AI edit proposal is stale and was not executed."),
            {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), error.message}}
        );
    } catch (const std::exception &error) {
        const QString cause = errorMessage(error);
        AIEditProposals::markStale(database, proposalId, cause);
        sink(runId, QStringLiteral("edit.stale"), {
            {QStringLiteral("proposalId"), proposalId},
            {QStringLiteral("approvedOperationIds"), approvedIds},
            {QStringLiteral("message"), cause},
            {QStringLiteral("writesPerformed"), false},
            {QStringLiteral("writesRetained"), false},
        });
        throw CoreError(
            QStringLiteral("The AI edit proposal is stale and was not executed."),
            {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
        );
    }

    sink(runId, QStringLiteral("edit.verified"), {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("approvedOperationIds"), approvedIds},
        {QStringLiteral("writesPerformed"), false},
        {QStringLiteral("writesRetained"), false},
    });

    QList<Rollback> rollbacks;
    QHash<QString, QVariant> afterHashes;
    QVariantMap afterHashObject;

    const auto failWith = [&](const QString &cause, bool stale) {
        const QStringList rollbackErrors = runRollbacks(rollbacks);

        if (!rollbackErrors.isEmpty()) {
            const QString text = QStringLiteral("AI edit execution could not be rolled back completely: %1").arg(rollbackErrors.join(QStringLiteral("; ")));
            AIEditProposals::markFailed(database, proposalId, QStringLiteral("rollback_failed"), text);
            sink(runId, QStringLiteral("edit.failed"), {
                {QStringLiteral("proposalId"), proposalId},
                {QStringLiteral("approvedOperationIds"), approvedIds},
                {QStringLiteral("errorCode"), QStringLiteral("rollback_failed")},
                {QStringLiteral("message"), text},
                {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
                {QStringLiteral("writesRetained"), true},
                {QStringLiteral("rollbackErrors"), toVariantList(rollbackErrors)},
            });
            throw CoreError(
                QStringLiteral("The AI edit transaction failed and rollback was incomplete."),
                {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}, {QStringLiteral("rollbackErrors"), toVariantList(rollbackErrors)}}
            );
        }

        if (stale) {
            AIEditProposals::markStale(database, proposalId, cause);
            sink(runId, QStringLiteral("edit.stale"), {
                {QStringLiteral("proposalId"), proposalId},
                {QStringLiteral("approvedOperationIds"), approvedIds},
                {QStringLiteral("message"), cause},
                {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
                {QStringLiteral("writesRetained"), false},
                {QStringLiteral("rolledBackOperationCount"), rollbacks.size()},
            });
            throw CoreError(
                QStringLiteral("The AI edit proposal became stale and was rolled back."),
                {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
            );
        }

        AIEditProposals::markFailed(database, proposalId, QStringLiteral("execution_failed"), cause);
        sink(runId, QStringLiteral("edit.failed"), {
            {QStringLiteral("proposalId"), proposalId},
            {QStringLiteral("approvedOperationIds"), approvedIds},
            {QStringLiteral("errorCode"), QStringLiteral("execution_failed")},
            {QStringLiteral("message"), cause},
            {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
            {QStringLiteral("writesRetained"), false},
            {QStringLiteral("rolledBackOperationCount"), rollbacks.size()},
        });
        throw CoreError(
            QStringLiteral("The AI edit transaction failed and was rolled back."),
            {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
        );
    };

    try {
        for (const QVariant &value : approved) {
            const QVariantMap operation = value.toMap();
            staleOnFailure([&]() { inspectOperation(database, executing, operation); });
            Executed executed;

            try {
                executed = executeOperation(rootPath, proposalId, operation);
            } catch (const FsError &error) {
                if (isStaleFilesystemError(error)) {
                    throw StaleError{error.message()};
                }

                throw;
            }

            rollbacks.append(executed.rollback);
            const QVariant hash = executed.absolutePath.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(hashFile(executed.absolutePath));
            const QString operationId = operation.value(QStringLiteral("id")).toString();
            afterHashes.insert(operationId, hash);
            afterHashObject.insert(operationId, hash);
        }

        AIEditProposals::completeExecution(database, proposalId, afterHashes);
    } catch (const StaleError &error) {
        failWith(error.message, true);
    } catch (const std::exception &error) {
        failWith(errorMessage(error), false);
    }

    const QVariantMap completed = AIEditProposals::requireProposal(database, proposalId);
    QVariantList changes;

    for (const QVariant &value : approved) {
        const QVariantMap operation = value.toMap();
        changes.append(QVariantMap{
            {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
            {QStringLiteral("type"), operation.value(QStringLiteral("type"))},
            {QStringLiteral("sourcePath"), operation.value(QStringLiteral("sourcePath"))},
            {QStringLiteral("destinationPath"), operation.value(QStringLiteral("destinationPath"))},
            {QStringLiteral("afterHash"), afterHashes.value(operation.value(QStringLiteral("id")).toString(), QVariant::fromValue(nullptr))},
        });
    }

    sink(runId, QStringLiteral("edit.completed"), {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("completedOperationIds"), approvedIds},
        {QStringLiteral("rejectedOperationIds"), rejectedIds},
        {QStringLiteral("changes"), changes},
        {QStringLiteral("affectedPaths"), uniquePaths(changes)},
        {QStringLiteral("afterHashes"), afterHashObject},
        {QStringLiteral("writesPerformed"), true},
        {QStringLiteral("writesRetained"), true},
    });

    const QVariantMap synchronization = synchronize(database, completed, true);
    QVariantMap payload = synchronizationPayload(synchronization);
    payload.insert(QStringLiteral("proposalId"), proposalId);
    sink(
        runId,
        synchronization.value(QStringLiteral("status")).toString() == QStringLiteral("synchronized") ? QStringLiteral("edit.synchronized")
                                                                                                     : QStringLiteral("edit.sync_warning"),
        payload
    );

    return {{QStringLiteral("proposal"), completed}, {QStringLiteral("synchronization"), synchronization}};
}

QVariantMap undo(CoreDatabase &database, const QString &proposalId, const EventSink &sink)
{
    const QVariantMap executing = AIEditProposals::beginUndo(database, proposalId);
    const QString runId = executing.value(QStringLiteral("runId")).toString();
    const QString libraryId = executing.value(QStringLiteral("libraryId")).toString();
    const QVariantList operations = operationsWithStatus(executing, QStringLiteral("completed"), true);
    const QVariantList operationIds = ids(operations);
    QString rootPath;

    const auto staleBeforeWrites = [&](const QString &cause) {
        AIEditProposals::restoreAfterUndoFailure(database, proposalId);
        sink(runId, QStringLiteral("edit.undo_stale"), {
            {QStringLiteral("proposalId"), proposalId},
            {QStringLiteral("operationIds"), operationIds},
            {QStringLiteral("message"), cause},
            {QStringLiteral("writesPerformed"), false},
            {QStringLiteral("forwardChangesRetained"), true},
        });
        throw CoreError(
            QStringLiteral("The completed AI edit transaction is stale and was not undone."),
            {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
        );
    };

    try {
        if (operations.isEmpty()) {
            throw CoreError(QStringLiteral("This AI edit transaction has no completed operations to undo."));
        }

        for (const QVariant &operation : operations) {
            staleOnFailure([&]() { inspectUndoOperation(database, executing, operation.toMap()); });
        }

        rootPath = canonicalRoot(database, libraryId);
    } catch (const StaleError &error) {
        staleBeforeWrites(error.message);
    } catch (const std::exception &error) {
        staleBeforeWrites(errorMessage(error));
    }

    sink(runId, QStringLiteral("edit.undo_verified"), {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("operationIds"), operationIds},
        {QStringLiteral("writesPerformed"), false},
        {QStringLiteral("forwardChangesRetained"), true},
    });

    QList<Rollback> rollbacks;

    const auto failWith = [&](const QString &cause, bool stale) {
        const QStringList rollbackErrors = runRollbacks(rollbacks);

        if (!rollbackErrors.isEmpty()) {
            const QString text = QStringLiteral("AI edit undo could not be rolled back completely: %1").arg(rollbackErrors.join(QStringLiteral("; ")));
            AIEditProposals::markUndoFailed(database, proposalId, QStringLiteral("undo_rollback_failed"), text);
            sink(runId, QStringLiteral("edit.undo_failed"), {
                {QStringLiteral("proposalId"), proposalId},
                {QStringLiteral("operationIds"), operationIds},
                {QStringLiteral("errorCode"), QStringLiteral("undo_rollback_failed")},
                {QStringLiteral("message"), text},
                {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
                {QStringLiteral("forwardChangesRetained"), false},
                {QStringLiteral("rollbackErrors"), toVariantList(rollbackErrors)},
            });
            throw CoreError(
                QStringLiteral("The AI edit undo failed and rollback was incomplete."),
                {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}, {QStringLiteral("rollbackErrors"), toVariantList(rollbackErrors)}}
            );
        }

        AIEditProposals::restoreAfterUndoFailure(database, proposalId);

        if (stale) {
            sink(runId, QStringLiteral("edit.undo_stale"), {
                {QStringLiteral("proposalId"), proposalId},
                {QStringLiteral("operationIds"), operationIds},
                {QStringLiteral("message"), cause},
                {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
                {QStringLiteral("forwardChangesRetained"), true},
                {QStringLiteral("rolledBackOperationCount"), rollbacks.size()},
            });
            throw CoreError(
                QStringLiteral("The completed AI edit transaction became stale and was not undone."),
                {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
            );
        }

        sink(runId, QStringLiteral("edit.undo_failed"), {
            {QStringLiteral("proposalId"), proposalId},
            {QStringLiteral("operationIds"), operationIds},
            {QStringLiteral("errorCode"), QStringLiteral("undo_failed")},
            {QStringLiteral("message"), cause},
            {QStringLiteral("writesPerformed"), !rollbacks.isEmpty()},
            {QStringLiteral("forwardChangesRetained"), true},
            {QStringLiteral("rolledBackOperationCount"), rollbacks.size()},
        });
        throw CoreError(
            QStringLiteral("The AI edit undo failed and its partial reversals were rolled back."),
            {{QStringLiteral("proposalId"), proposalId}, {QStringLiteral("cause"), cause}}
        );
    };

    try {
        for (const QVariant &value : operations) {
            const QVariantMap operation = value.toMap();
            staleOnFailure([&]() { inspectUndoOperation(database, executing, operation); });

            try {
                rollbacks.append(executeUndoOperation(rootPath, proposalId, operation));
            } catch (const FsError &error) {
                if (isStaleFilesystemError(error)) {
                    throw StaleError{error.message()};
                }

                throw;
            }
        }
    } catch (const StaleError &error) {
        failWith(error.message, true);
    } catch (const std::exception &error) {
        failWith(errorMessage(error), false);
    }

    const QVariantMap undone = AIEditProposals::completeUndo(database, proposalId);
    QVariantList changes;

    for (const QVariant &value : operations) {
        const QVariantMap operation = value.toMap();
        const QString type = operation.value(QStringLiteral("type")).toString();
        QVariantMap change{{QStringLiteral("operationId"), operation.value(QStringLiteral("id"))}, {QStringLiteral("type"), type}};

        if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
            change.insert(QStringLiteral("sourcePath"), operation.value(QStringLiteral("destinationPath")));
            change.insert(QStringLiteral("destinationPath"), operation.value(QStringLiteral("sourcePath")));
            change.insert(QStringLiteral("restoredHash"), operation.value(QStringLiteral("expectedHash")));
        } else if (type == QStringLiteral("patch_file")) {
            change.insert(QStringLiteral("sourcePath"), operation.value(QStringLiteral("sourcePath")));
            change.insert(QStringLiteral("destinationPath"), operation.value(QStringLiteral("destinationPath")));
            change.insert(QStringLiteral("restoredHash"), operation.value(QStringLiteral("expectedHash")));
        } else {
            change.insert(QStringLiteral("sourcePath"), operation.value(QStringLiteral("destinationPath")));
            change.insert(QStringLiteral("destinationPath"), QVariant::fromValue(nullptr));
            change.insert(QStringLiteral("restoredHash"), QVariant::fromValue(nullptr));
        }

        changes.append(change);
    }

    sink(runId, QStringLiteral("edit.undone"), {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("undoneOperationIds"), operationIds},
        {QStringLiteral("changes"), changes},
        {QStringLiteral("affectedPaths"), uniquePaths(changes)},
        {QStringLiteral("writesPerformed"), true},
        {QStringLiteral("forwardChangesRetained"), false},
    });

    const QVariantMap synchronization = synchronize(database, undone, false);
    QVariantMap payload = synchronizationPayload(synchronization);
    payload.insert(QStringLiteral("proposalId"), proposalId);
    payload.insert(QStringLiteral("direction"), QStringLiteral("undo"));
    sink(
        runId,
        synchronization.value(QStringLiteral("status")).toString() == QStringLiteral("synchronized") ? QStringLiteral("edit.synchronized")
                                                                                                     : QStringLiteral("edit.sync_warning"),
        payload
    );

    return {{QStringLiteral("proposal"), undone}, {QStringLiteral("synchronization"), synchronization}};
}
}
