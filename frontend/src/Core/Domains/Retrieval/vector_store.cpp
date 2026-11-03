#include "vector_store.h"

#include "vector_codec.h"

#include "Core/Support/core_values.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

namespace
{
bool better(const Retrieval::VectorHit &left, const Retrieval::VectorHit &right)
{
    if (left.similarity != right.similarity) {
        return left.similarity > right.similarity;
    }

    return left.sourceId < right.sourceId;
}
}

namespace Retrieval
{
VectorSearchResult ExactVectorStore::search(CoreDatabase &database, const VectorQuery &query) const
{
    QElapsedTimer timer;
    timer.start();
    VectorSearchResult result;

    if (query.scopeIds.isEmpty() || query.limit <= 0 || query.vector.size() != query.config.dimensions) {
        return result;
    }

    const QStringList scopes = CoreValues::uniqueIds(query.scopeIds);
    auto statement = database.prepare(QStringLiteral(
        "SELECT s.source_type, s.source_id, s.scope_id, s.content_hash, v.vector FROM embedding_sources s "
        "INNER JOIN embedding_vectors v ON v.content_hash = s.content_hash AND v.model = ? AND v.dimensions = ? "
        "AND v.input_version = ? WHERE s.source_type = ? AND s.scope_type = ? AND s.scope_id IN (%1) LIMIT ?"
    ).arg(CoreValues::placeholders(scopes.size())));
    int parameter = 1;
    statement.bind(parameter++, query.config.model)
        .bind(parameter++, query.config.dimensions)
        .bind(parameter++, query.config.inputVersion)
        .bind(parameter++, query.sourceType)
        .bind(parameter++, query.scopeType);

    for (const QString &scope : scopes) {
        statement.bind(parameter++, scope);
    }

    const int maxScanned = std::max(1, query.maxScanned);
    statement.bind(parameter, maxScanned + 1);

    // A bounded heap of the best `limit` hits: the worst kept hit is at the
    // front so a better one can replace it in O(log limit).
    QList<VectorHit> heap;
    heap.reserve(query.limit + 1);
    const auto worstFirst = [](const VectorHit &left, const VectorHit &right) { return better(left, right); };

    while (statement.step()) {
        if (result.scanned >= maxScanned) {
            result.truncated = true;
            break;
        }

        ++result.scanned;
        const QByteArray blob = statement.value(4).toByteArray();
        result.bytesScanned += blob.size();
        const double similarity = VectorCodec::dot(query.vector, blob);

        if (!std::isfinite(similarity) || similarity < query.minSimilarity) {
            continue;
        }

        VectorHit hit{statement.value(0).toString(), statement.value(1).toString(), statement.value(2).toString(), statement.value(3).toString(), similarity};

        if (heap.size() < query.limit) {
            heap.append(hit);
            std::push_heap(heap.begin(), heap.end(), worstFirst);
        } else if (better(hit, heap.front())) {
            std::pop_heap(heap.begin(), heap.end(), worstFirst);
            heap.back() = hit;
            std::push_heap(heap.begin(), heap.end(), worstFirst);
        }
    }

    std::sort(heap.begin(), heap.end(), better);
    result.hits = heap;
    result.durationMs = std::round(timer.nsecsElapsed() / 1000.0) / 1000.0;
    return result;
}

const VectorStore &defaultVectorStore()
{
    static const ExactVectorStore store;
    return store;
}
}
