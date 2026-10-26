#pragma once

#include "library_file_repository.h"

#include <QList>
#include <QString>

// Native port of backend/src/api/libraries/services/LibraryTextChunker.ts.
// Chunk ids and hashes are byte-for-byte what the Node indexer produced, so
// both sides agree while they share archivist.db.
namespace LibraryTextChunker
{
[[nodiscard]] QString normalize(const QString &content);
[[nodiscard]] QString hash(const QString &content);
[[nodiscard]] int estimateTokens(const QString &content);
[[nodiscard]] QList<LibraryFileRepository::TextChunk> chunk(
    const QString &libraryId,
    const QString &libraryFileId,
    const QString &extension,
    const QString &content
);
}
