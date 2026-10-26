#include "library_file_repository.h"

#include "Core/Support/core_values.h"
#include "Core/core_error.h"

using namespace CoreValues;

namespace
{
const QString fileColumns = QStringLiteral(
    "id, library_id, relative_path, name, extension, size_bytes, modified_at, status, last_seen_at, created_at, updated_at"
);
const QString scanColumns = QStringLiteral(
    "id, library_id, status, started_at, completed_at, discovered_file_count, cataloged_file_count, "
    "ignored_entry_count, error_count, error_message"
);
const QString documentColumns = QStringLiteral(
    "library_file_id, library_id, status, content_hash, source_modified_at, source_size_bytes, chunk_count, "
    "error_message, extracted_at, updated_at"
);
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

QVariantMap mapFile(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("relativePath"), row.value(QStringLiteral("relative_path"))},
        {QStringLiteral("name"), row.value(QStringLiteral("name"))},
        {QStringLiteral("extension"), row.value(QStringLiteral("extension"))},
        {QStringLiteral("sizeBytes"), row.value(QStringLiteral("size_bytes")).toLongLong()},
        {QStringLiteral("modifiedAt"), row.value(QStringLiteral("modified_at"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("lastSeenAt"), nullable(row.value(QStringLiteral("last_seen_at")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

QVariantMap mapScan(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("startedAt"), row.value(QStringLiteral("started_at"))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("discoveredFileCount"), row.value(QStringLiteral("discovered_file_count")).toLongLong()},
        {QStringLiteral("catalogedFileCount"), row.value(QStringLiteral("cataloged_file_count")).toLongLong()},
        {QStringLiteral("ignoredEntryCount"), row.value(QStringLiteral("ignored_entry_count")).toLongLong()},
        {QStringLiteral("errorCount"), row.value(QStringLiteral("error_count")).toLongLong()},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
    };
}

QVariantMap mapDocument(const QVariantMap &row)
{
    return {
        {QStringLiteral("libraryFileId"), row.value(QStringLiteral("library_file_id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("contentHash"), nullable(row.value(QStringLiteral("content_hash")))},
        {QStringLiteral("sourceModifiedAt"), row.value(QStringLiteral("source_modified_at"))},
        {QStringLiteral("sourceSizeBytes"), row.value(QStringLiteral("source_size_bytes")).toLongLong()},
        {QStringLiteral("chunkCount"), row.value(QStringLiteral("chunk_count")).toLongLong()},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
        {QStringLiteral("extractedAt"), row.value(QStringLiteral("extracted_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

std::optional<QVariantMap> scanById(CoreDatabase &database, const QString &scanId)
{
    const auto row =
        database.prepare(QStringLiteral("SELECT %1 FROM library_scans WHERE id = ?").arg(scanColumns)).bind(1, scanId).one();
    return row ? std::optional<QVariantMap>(mapScan(*row)) : std::nullopt;
}

QVariantMap requireScan(CoreDatabase &database, const QString &scanId, const QString &failure)
{
    const auto scan = scanById(database, scanId);

    if (!scan) {
        throw CoreError(failure);
    }

    return *scan;
}

const QString documentUpsert = QStringLiteral(
    "INSERT INTO library_documents (library_file_id, library_id, status, content_hash, source_modified_at, "
    "source_size_bytes, chunk_count, error_message, extracted_at, updated_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, %1, %1) "
    "ON CONFLICT (library_file_id) DO UPDATE SET library_id = excluded.library_id, status = excluded.status, "
    "content_hash = excluded.content_hash, source_modified_at = excluded.source_modified_at, "
    "source_size_bytes = excluded.source_size_bytes, chunk_count = excluded.chunk_count, "
    "error_message = excluded.error_message, extracted_at = excluded.extracted_at, updated_at = excluded.updated_at"
);
}

namespace LibraryFileRepository
{
QVariantList files(CoreDatabase &database, const QString &libraryId)
{
    QVariantList result;

    for (const QVariantMap &row :
         database
             .prepare(QStringLiteral("SELECT %1 FROM library_files WHERE library_id = ? ORDER BY relative_path COLLATE NOCASE ASC")
                          .arg(fileColumns))
             .bind(1, libraryId)
             .all()) {
        result.append(mapFile(row));
    }

    return result;
}

QVariant latestScan(CoreDatabase &database, const QString &libraryId)
{
    const auto row = database
                         .prepare(QStringLiteral(
                                      "SELECT %1 FROM library_scans WHERE library_id = ? "
                                      "ORDER BY started_at DESC, rowid DESC LIMIT 1"
                         )
                                      .arg(scanColumns))
                         .bind(1, libraryId)
                         .one();
    return row ? QVariant(mapScan(*row)) : QVariant::fromValue(nullptr);
}

QVariantMap catalog(CoreDatabase &database, const QString &libraryId)
{
    return {
        {QStringLiteral("files"), files(database, libraryId)},
        {QStringLiteral("latestScan"), latestScan(database, libraryId)},
    };
}

std::optional<QVariantMap> fileById(CoreDatabase &database, const QString &libraryId, const QString &fileId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM library_files WHERE id = ? AND library_id = ?").arg(fileColumns))
                         .bind(1, fileId)
                         .bind(2, libraryId)
                         .one();
    return row ? std::optional<QVariantMap>(mapFile(*row)) : std::nullopt;
}

std::optional<QVariantMap> fileByRelativePath(CoreDatabase &database, const QString &libraryId, const QString &relativePath)
{
    const auto row =
        database.prepare(QStringLiteral("SELECT %1 FROM library_files WHERE library_id = ? AND relative_path = ?").arg(fileColumns))
            .bind(1, libraryId)
            .bind(2, relativePath)
            .one();
    return row ? std::optional<QVariantMap>(mapFile(*row)) : std::nullopt;
}

QVariantMap updateFileLocation(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fileId,
    const QString &relativePath,
    const QString &name,
    const QString &extension
)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE library_files SET relative_path = ?, name = ?, extension = ?, status = 'available', "
                     "updated_at = %1 WHERE id = ? AND library_id = ?"
        )
                     .arg(nowSql))
        .bind(1, relativePath)
        .bind(2, name)
        .bind(3, extension)
        .bind(4, fileId)
        .bind(5, libraryId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    const auto file = fileById(database, libraryId, fileId);

    if (!file) {
        throw CoreError(QStringLiteral("The moved Library file could not be loaded."));
    }

    return *file;
}

QVariantMap createScan(CoreDatabase &database, const QString &libraryId)
{
    const auto running =
        database.prepare(QStringLiteral("SELECT id FROM library_scans WHERE library_id = ? AND status = 'running' LIMIT 1"))
            .bind(1, libraryId)
            .one();

    if (running) {
        throw CoreError(QStringLiteral("This Library is already being scanned."));
    }

    const QString scanId = newId();
    database.prepare(QStringLiteral("INSERT INTO library_scans (id, library_id, status) VALUES (?, ?, 'running')"))
        .bind(1, scanId)
        .bind(2, libraryId)
        .run();

    return requireScan(database, scanId, QStringLiteral("The new Library scan could not be loaded."));
}

QVariantMap completeScan(
    CoreDatabase &database,
    const QString &scanId,
    const QString &libraryId,
    const QList<ScannedFile> &files,
    int discoveredFileCount,
    int ignoredEntryCount,
    int errorCount
)
{
    auto upsert = database.prepare(QStringLiteral(
                                       "INSERT INTO library_files (id, library_id, relative_path, name, extension, size_bytes, "
                                       "modified_at, status, last_seen_scan_id, last_seen_at) "
                                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, %1) "
                                       "ON CONFLICT (library_id, relative_path) DO UPDATE SET name = excluded.name, "
                                       "extension = excluded.extension, size_bytes = excluded.size_bytes, "
                                       "modified_at = excluded.modified_at, status = excluded.status, "
                                       "last_seen_scan_id = excluded.last_seen_scan_id, last_seen_at = excluded.last_seen_at, "
                                       "updated_at = %1"
    )
                                       .arg(nowSql));

    for (const ScannedFile &file : files) {
        upsert.bind(1, newId())
            .bind(2, libraryId)
            .bind(3, file.relativePath)
            .bind(4, file.name)
            .bind(5, file.extension)
            .bind(6, file.sizeBytes)
            .bind(7, file.modifiedAt)
            .bind(8, file.status)
            .bind(9, scanId)
            .run();
        upsert.reset();
    }

    if (errorCount == 0) {
        database
            .prepare(QStringLiteral(
                         "UPDATE library_files SET status = 'missing', updated_at = %1 WHERE library_id = ? "
                         "AND (last_seen_scan_id IS NULL OR last_seen_scan_id != ?) AND status != 'missing'"
            )
                         .arg(nowSql))
            .bind(1, libraryId)
            .bind(2, scanId)
            .run();
    }

    const QVariant errorMessage = errorCount > 0
        ? QVariant(QStringLiteral("The scan completed with %1 issue%2.")
                       .arg(errorCount)
                       .arg(errorCount == 1 ? QString() : QStringLiteral("s")))
        : QVariant();

    database
        .prepare(QStringLiteral(
                     "UPDATE library_scans SET status = ?, completed_at = %1, discovered_file_count = ?, "
                     "cataloged_file_count = ?, ignored_entry_count = ?, error_count = ?, error_message = ? "
                     "WHERE id = ? AND library_id = ? AND status = 'running'"
        )
                     .arg(nowSql))
        .bind(1, errorCount > 0 ? QStringLiteral("partial") : QStringLiteral("complete"))
        .bind(2, discoveredFileCount)
        .bind(3, files.size())
        .bind(4, ignoredEntryCount)
        .bind(5, errorCount)
        .bind(6, errorMessage)
        .bind(7, scanId)
        .bind(8, libraryId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("The Library scan could not be completed."));
    }

    return requireScan(database, scanId, QStringLiteral("The completed Library scan could not be loaded."));
}

QVariantMap failScan(CoreDatabase &database, const QString &scanId, const QString &libraryId, const QString &errorMessage)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE library_scans SET status = 'failed', completed_at = %1, error_count = 1, error_message = ? "
                     "WHERE id = ? AND library_id = ? AND status = 'running'"
        )
                     .arg(nowSql))
        .bind(1, errorMessage)
        .bind(2, scanId)
        .bind(3, libraryId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("The failed Library scan could not be recorded."));
    }

    return requireScan(database, scanId, QStringLiteral("The failed Library scan could not be loaded."));
}

std::optional<QVariantMap> textDocument(CoreDatabase &database, const QString &libraryFileId)
{
    const auto row =
        database.prepare(QStringLiteral("SELECT %1 FROM library_documents WHERE library_file_id = ?").arg(documentColumns))
            .bind(1, libraryFileId)
            .one();
    return row ? std::optional<QVariantMap>(mapDocument(*row)) : std::nullopt;
}

void touchTextDocument(CoreDatabase &database, const QString &libraryFileId, const QString &sourceModifiedAt, qint64 sourceSizeBytes)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE library_documents SET source_modified_at = ?, source_size_bytes = ?, extracted_at = %1, "
                     "updated_at = %1 WHERE library_file_id = ?"
        )
                     .arg(nowSql))
        .bind(1, sourceModifiedAt)
        .bind(2, sourceSizeBytes)
        .bind(3, libraryFileId)
        .run();
}

void replaceTextDocument(
    CoreDatabase &database,
    const QString &libraryFileId,
    const QString &libraryId,
    const QString &status,
    const QString &contentHash,
    const QString &sourceModifiedAt,
    qint64 sourceSizeBytes,
    const QList<TextChunk> &chunks
)
{
    database.prepare(documentUpsert.arg(nowSql))
        .bind(1, libraryFileId)
        .bind(2, libraryId)
        .bind(3, status)
        .bind(4, contentHash)
        .bind(5, sourceModifiedAt)
        .bind(6, sourceSizeBytes)
        .bind(7, chunks.size())
        .bind(8, QVariant())
        .run();

    database.prepare(QStringLiteral("DELETE FROM library_chunks WHERE library_file_id = ?")).bind(1, libraryFileId).run();

    auto insert = database.prepare(QStringLiteral(
        "INSERT INTO library_chunks (id, library_file_id, library_id, ordinal, start_line, end_line, content, "
        "estimated_tokens, content_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ));

    for (const TextChunk &chunk : chunks) {
        insert.bind(1, chunk.id)
            .bind(2, chunk.libraryFileId)
            .bind(3, chunk.libraryId)
            .bind(4, chunk.ordinal)
            .bind(5, chunk.startLine)
            .bind(6, chunk.endLine)
            .bind(7, chunk.content)
            .bind(8, chunk.estimatedTokens)
            .bind(9, chunk.contentHash)
            .run();
        insert.reset();
    }
}

void markTextDocument(
    CoreDatabase &database,
    const QString &libraryFileId,
    const QString &libraryId,
    const QString &status,
    const QString &sourceModifiedAt,
    qint64 sourceSizeBytes,
    const QString &errorMessage
)
{
    database.prepare(documentUpsert.arg(nowSql))
        .bind(1, libraryFileId)
        .bind(2, libraryId)
        .bind(3, status)
        .bind(4, QVariant())
        .bind(5, sourceModifiedAt)
        .bind(6, sourceSizeBytes)
        .bind(7, 0)
        .bind(8, errorMessage)
        .run();

    database.prepare(QStringLiteral("DELETE FROM library_chunks WHERE library_file_id = ?")).bind(1, libraryFileId).run();
}
}
