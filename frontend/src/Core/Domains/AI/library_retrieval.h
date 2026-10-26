#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantMap>

// Native port of backend/src/core/cognition/retrieval/tools/LibraryFileFtsTool.ts:
// FTS5 search over indexed Library chunks with subject-aware reranking.
namespace LibraryRetrieval
{
inline const QString toolId = QStringLiteral("library-file-fts");

// {tool, query, candidates, searchedAt, durationMs, warnings}. `limit` <= 0
// uses the default of 12.
[[nodiscard]] QVariantMap search(CoreDatabase &database, const QString &query, const QString &libraryId, int limit = 0);
}
