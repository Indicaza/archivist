#pragma once

#include "Core/Database/core_database.h"

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of the database half of backend/src/api/libraries:
// models/LibraryFile.ts (catalog and scans) and models/LibraryTextIndex.ts.
// Files are {id, libraryId, relativePath, name, extension, sizeBytes,
// modifiedAt, status, lastSeenAt, createdAt, updatedAt}.
namespace LibraryFileRepository
{
struct ScannedFile
{
    QString relativePath;
    QString name;
    QString extension;
    qint64 sizeBytes = 0;
    QString modifiedAt;
    QString status; // "available" or "unreadable"
};

struct TextChunk
{
    QString id;
    QString libraryFileId;
    QString libraryId;
    int ordinal = 0;
    int startLine = 0;
    int endLine = 0;
    QString content;
    int estimatedTokens = 0;
    QString contentHash;
};

// {files, latestScan}
[[nodiscard]] QVariantMap catalog(CoreDatabase &database, const QString &libraryId);
[[nodiscard]] QVariantList files(CoreDatabase &database, const QString &libraryId);
[[nodiscard]] QVariant latestScan(CoreDatabase &database, const QString &libraryId);
[[nodiscard]] std::optional<QVariantMap> fileById(CoreDatabase &database, const QString &libraryId, const QString &fileId);
[[nodiscard]] std::optional<QVariantMap> fileByRelativePath(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &relativePath
);
QVariantMap updateFileLocation(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fileId,
    const QString &relativePath,
    const QString &name,
    const QString &extension
);

QVariantMap createScan(CoreDatabase &database, const QString &libraryId);
QVariantMap completeScan(
    CoreDatabase &database,
    const QString &scanId,
    const QString &libraryId,
    const QList<ScannedFile> &files,
    int discoveredFileCount,
    int ignoredEntryCount,
    int errorCount
);
QVariantMap failScan(CoreDatabase &database, const QString &scanId, const QString &libraryId, const QString &errorMessage);

// Text index documents: {libraryFileId, libraryId, status, contentHash,
// sourceModifiedAt, sourceSizeBytes, chunkCount, errorMessage, extractedAt, updatedAt}.
[[nodiscard]] std::optional<QVariantMap> textDocument(CoreDatabase &database, const QString &libraryFileId);
void touchTextDocument(CoreDatabase &database, const QString &libraryFileId, const QString &sourceModifiedAt, qint64 sourceSizeBytes);
void replaceTextDocument(
    CoreDatabase &database,
    const QString &libraryFileId,
    const QString &libraryId,
    const QString &status,
    const QString &contentHash,
    const QString &sourceModifiedAt,
    qint64 sourceSizeBytes,
    const QList<TextChunk> &chunks
);
void markTextDocument(
    CoreDatabase &database,
    const QString &libraryFileId,
    const QString &libraryId,
    const QString &status,
    const QString &sourceModifiedAt,
    qint64 sourceSizeBytes,
    const QString &errorMessage
);
}
