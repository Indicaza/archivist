#pragma once

#include "Core/Database/core_database.h"
#include "embedding_config.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// Nearest-neighbor search over stored vectors, behind an interface so an
// optimized backend (a SQLite vector extension, an ANN index) can replace the
// exact scan without touching the indexer or hybrid search.
namespace Retrieval
{
struct VectorQuery
{
    QVector<float> vector;   // unit length, config.dimensions long
    EmbeddingConfig config;
    QString sourceType;      // e.g. SourceType::libraryChunk
    QString scopeType;       // e.g. ScopeType::library
    QStringList scopeIds;    // only these scopes are read; empty finds nothing
    int limit = 24;
    double minSimilarity = 0; // cosine similarity floor
    // Upper bound on rows read, so one query has a known worst case. Rows
    // past it are not scored and the result says it was truncated.
    int maxScanned = 200000;
};

struct VectorHit
{
    QString sourceType;
    QString sourceId;
    QString scopeId;
    QString contentHash;
    double similarity = 0;
};

struct VectorSearchResult
{
    QList<VectorHit> hits;   // best first; equal similarity by source id
    int scanned = 0;
    qint64 bytesScanned = 0;
    bool truncated = false;
    double durationMs = 0;
};

class VectorStore
{
public:
    virtual ~VectorStore() = default;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual VectorSearchResult search(CoreDatabase &database, const VectorQuery &query) const = 0;
};

// Reads every vector in scope and keeps the best `limit`. Memory stays at
// one row plus the result list; time grows linearly with the scope.
class ExactVectorStore final : public VectorStore
{
public:
    [[nodiscard]] QString name() const override { return QStringLiteral("exact-sqlite"); }
    [[nodiscard]] VectorSearchResult search(CoreDatabase &database, const VectorQuery &query) const override;
};

[[nodiscard]] const VectorStore &defaultVectorStore();
}
