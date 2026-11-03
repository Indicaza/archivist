#include "hybrid_retrieval.h"

#include "embedding_index.h"

#include "Core/Domains/AI/library_retrieval.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/core_values.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace
{
double configPercent(const QVariantMap &config, const char *key, double fallback)
{
    const QVariant value = config.value(QLatin1String(key));
    bool ok = false;
    const double number = value.toDouble(&ok);

    if (!value.isValid() || !ok || value.typeId() == QMetaType::QString || !std::isfinite(number)) {
        return fallback;
    }

    return std::clamp(number, 0.0, 100.0) / 100.0;
}

double milliseconds(const QElapsedTimer &timer)
{
    return std::round(timer.nsecsElapsed() / 1000.0) / 1000.0;
}

QString hitReason(const Retrieval::HybridHit &hit)
{
    QStringList parts;

    if (hit.exact == Retrieval::ExactMatch::FileName) {
        parts.append(QStringLiteral("exact file name"));
    } else if (hit.exact == Retrieval::ExactMatch::Identifier) {
        parts.append(QStringLiteral("exact identifier"));
    }

    if (hit.lexicalRank > 0) {
        parts.append(QStringLiteral("keyword #%1").arg(hit.lexicalRank));
    }

    if (hit.semanticRank > 0) {
        parts.append(QStringLiteral("semantic #%1 (similarity %2)").arg(hit.semanticRank).arg(hit.similarity, 0, 'f', 3));
    }

    return QStringLiteral("Hybrid Library search (reciprocal rank fusion): ") + parts.join(QStringLiteral(", ")) + QLatin1Char('.');
}
}

namespace Retrieval
{
QString exactMatchName(ExactMatch exact)
{
    switch (exact) {
    case ExactMatch::FileName:
        return QStringLiteral("file-name");
    case ExactMatch::Identifier:
        return QStringLiteral("identifier");
    case ExactMatch::None:
        break;
    }

    return QString();
}

HybridPolicy HybridPolicy::fromConfig(const QVariantMap &config)
{
    HybridPolicy policy;
    policy.semanticEnabled = config.value(QStringLiteral("semanticRetrieval")).toString() != QStringLiteral("off");
    policy.lexicalWeight = configPercent(config, "lexicalWeight", policy.lexicalWeight);
    policy.semanticWeight = configPercent(config, "semanticWeight", policy.semanticWeight);
    policy.minSimilarity = configPercent(config, "semanticMinSimilarity", policy.minSimilarity);

    if (policy.semanticWeight <= 0) {
        policy.semanticEnabled = false;
    }

    return policy;
}

QVariantMap HybridPolicy::toVariant() const
{
    return {
        {QStringLiteral("semanticEnabled"), semanticEnabled},
        {QStringLiteral("lexicalWeight"), lexicalWeight},
        {QStringLiteral("semanticWeight"), semanticWeight},
        {QStringLiteral("minSimilarity"), minSimilarity},
        {QStringLiteral("rrfK"), rrfK},
        {QStringLiteral("lexicalLimit"), lexicalLimit},
        {QStringLiteral("semanticLimit"), semanticLimit},
        {QStringLiteral("exactMatchBoost"), exactMatchBoost},
        {QStringLiteral("maxScannedVectors"), maxScannedVectors},
    };
}

QStringList exactTerms(const QString &query)
{
    static const QRegularExpression backticked(QStringLiteral("`([^`\\n]{2,120})`"));
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}_][\\p{L}\\p{N}_.:/\\-]*[\\p{L}\\p{N}_]"));
    static const QRegularExpression fileName(QStringLiteral("^[\\p{L}\\p{N}_\\-./]+\\.[A-Za-z][A-Za-z0-9]{0,7}$"));
    static const QRegularExpression innerCapital(QStringLiteral("\\p{Ll}\\p{Lu}|\\p{Lu}\\p{Lu}\\p{Ll}"));
    QStringList terms;
    QSet<QString> seen;
    const auto add = [&](const QString &term) {
        if (term.size() >= 3 && !seen.contains(term)) {
            seen.insert(term);
            terms.append(term);
        }
    };

    auto quoted = backticked.globalMatch(query);

    while (quoted.hasNext()) {
        add(quoted.next().captured(1).trimmed());
    }

    auto words = word.globalMatch(query);

    while (words.hasNext()) {
        QString token = words.next().captured(0);

        while (token.endsWith(QLatin1Char('.')) || token.endsWith(QLatin1Char(':'))) {
            token.chop(1);
        }

        const bool qualified = token.contains(QStringLiteral("::")) || token.contains(QStringLiteral("->"));
        const bool snake = token.contains(QLatin1Char('_')) && token.size() > 3;
        const bool camel = innerCapital.match(token).hasMatch();
        const bool file = fileName.match(token).hasMatch() && !token.startsWith(QLatin1Char('.'));

        if (qualified || snake || camel || file) {
            add(token);
        }
    }

    return terms.mid(0, 12);
}

ExactMatch exactMatch(const QStringList &terms, const QString &relativePath, const QString &fileName, const QString &content)
{
    ExactMatch best = ExactMatch::None;

    for (const QString &term : terms) {
        const bool looksLikeFile = term.contains(QLatin1Char('.')) && !term.contains(QStringLiteral("::"));

        if (looksLikeFile
            && (fileName.compare(term, Qt::CaseInsensitive) == 0
                || relativePath.endsWith(QLatin1Char('/') + term, Qt::CaseInsensitive)
                || relativePath.compare(term, Qt::CaseInsensitive) == 0)) {
            return ExactMatch::FileName;
        }

        // Case-sensitive, not inside a longer identifier.
        const QRegularExpression bounded(
            QStringLiteral("(?<![\\p{L}\\p{N}_])") + QRegularExpression::escape(term) + QStringLiteral("(?![\\p{L}\\p{N}_])")
        );

        if (bounded.match(content).hasMatch() || bounded.match(relativePath).hasMatch()) {
            best = ExactMatch::Identifier;
        }
    }

    return best;
}

QList<FusedRank> fuse(const QList<FusionInput> &inputs, const HybridPolicy &policy)
{
    QList<FusedRank> fused;
    const double k = std::max(1, policy.rrfK);

    for (const FusionInput &input : inputs) {
        FusedRank rank;
        rank.id = input.id;
        rank.lexicalRank = input.lexicalRank;
        rank.semanticRank = input.semanticRank;
        rank.exact = input.exact;

        if (input.lexicalRank > 0) {
            rank.score += policy.lexicalWeight / (k + input.lexicalRank);
        }

        if (input.semanticRank > 0) {
            rank.score += policy.semanticWeight / (k + input.semanticRank);
        }

        if (input.exact == ExactMatch::FileName) {
            rank.score += policy.exactMatchBoost;
        } else if (input.exact == ExactMatch::Identifier) {
            rank.score += policy.exactMatchBoost / 2;
        }

        fused.append(rank);
    }

    std::stable_sort(fused.begin(), fused.end(), [](const FusedRank &left, const FusedRank &right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }

        const int leftLexical = left.lexicalRank > 0 ? left.lexicalRank : INT_MAX;
        const int rightLexical = right.lexicalRank > 0 ? right.lexicalRank : INT_MAX;

        if (leftLexical != rightLexical) {
            return leftLexical < rightLexical;
        }

        return left.id < right.id;
    });
    return fused;
}

bool semanticAvailable(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config)
{
    if (libraryId.isEmpty() || !EmbeddingIndex::isLibraryEnabled(database, libraryId)) {
        return false;
    }

    return database
               .prepare(QStringLiteral(
                   "SELECT EXISTS (SELECT 1 FROM embedding_sources s INNER JOIN embedding_vectors v "
                   "ON v.content_hash = s.content_hash AND v.model = ? AND v.dimensions = ? AND v.input_version = ? "
                   "WHERE s.scope_type = 'library' AND s.scope_id = ? AND s.source_type = 'library-chunk')"
               ))
               .bind(1, config.model)
               .bind(2, config.dimensions)
               .bind(3, config.inputVersion)
               .bind(4, libraryId)
               .scalar()
               .toInt()
        == 1;
}

HybridResult searchLibrary(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &query,
    const QVector<float> &queryVector,
    const HybridPolicy &policy,
    int limit,
    const EmbeddingConfig &config,
    const VectorStore &store
)
{
    QElapsedTimer total;
    total.start();
    LiveEvents::Scope live(QStringLiteral("retrieval"), QStringLiteral("hybrid-search"));
    HybridResult result;
    const int clampedLimit = std::clamp(limit, 1, 50);
    const bool semantic = policy.semanticEnabled && policy.semanticWeight > 0 && queryVector.size() == config.dimensions;
    QHash<QString, HybridHit> hits;
    QStringList order;

    // 1. Keyword search: the existing FTS5 path, unchanged.
    QElapsedTimer lexicalTimer;
    lexicalTimer.start();
    const QVariantMap lexical = LibraryRetrieval::search(database, query, libraryId, std::max(clampedLimit, policy.lexicalLimit));
    const double lexicalMs = milliseconds(lexicalTimer);

    for (const QVariant &warning : lexical.value(QStringLiteral("warnings")).toList()) {
        result.warnings.append(warning.toString());
    }

    int lexicalRank = 0;

    for (const QVariant &value : lexical.value(QStringLiteral("candidates")).toList()) {
        const QVariantMap candidate = value.toMap();
        const QVariantMap metadata = candidate.value(QStringLiteral("metadata")).toMap();
        HybridHit hit;
        hit.chunkId = candidate.value(QStringLiteral("id")).toString();
        hit.libraryId = metadata.value(QStringLiteral("libraryId")).toString();
        hit.fileId = metadata.value(QStringLiteral("fileId")).toString();
        hit.relativePath = metadata.value(QStringLiteral("relativePath")).toString();
        hit.fileName = metadata.value(QStringLiteral("fileName")).toString();
        hit.content = candidate.value(QStringLiteral("content")).toString();
        hit.contentHash = metadata.value(QStringLiteral("contentHash")).toString();
        hit.startLine = metadata.value(QStringLiteral("startLine")).toInt();
        hit.endLine = metadata.value(QStringLiteral("endLine")).toInt();
        hit.lexicalRank = ++lexicalRank;

        if (hit.libraryId.isEmpty()) {
            hit.libraryId = libraryId;
        }

        if (!hits.contains(hit.chunkId)) {
            order.append(hit.chunkId);
            hits.insert(hit.chunkId, hit);
        }
    }

    // 2. Semantic search over this Library's vectors only.
    double vectorMs = 0;
    int semanticCandidates = 0;

    if (semantic) {
        VectorQuery vectorQuery;
        vectorQuery.vector = queryVector;
        vectorQuery.config = config;
        vectorQuery.sourceType = SourceType::libraryChunk;
        vectorQuery.scopeType = ScopeType::library;
        vectorQuery.scopeIds = {libraryId};
        vectorQuery.limit = std::max(clampedLimit, policy.semanticLimit);
        vectorQuery.minSimilarity = policy.minSimilarity;
        vectorQuery.maxScanned = policy.maxScannedVectors;
        const VectorSearchResult vectors = store.search(database, vectorQuery);
        vectorMs = vectors.durationMs;
        semanticCandidates = static_cast<int>(vectors.hits.size());
        result.diagnostics.insert(QStringLiteral("vectorsScanned"), vectors.scanned);
        result.diagnostics.insert(QStringLiteral("bytesScanned"), vectors.bytesScanned);
        result.diagnostics.insert(QStringLiteral("truncated"), vectors.truncated);

        if (vectors.truncated) {
            result.warnings.append(QStringLiteral("Semantic search read the first %1 vectors of this Library and skipped the rest.").arg(vectors.scanned));
        }

        QStringList missing;
        int semanticRank = 0;

        for (const VectorHit &vectorHit : vectors.hits) {
            ++semanticRank;

            if (hits.contains(vectorHit.sourceId)) {
                HybridHit &hit = hits[vectorHit.sourceId];
                hit.semanticRank = semanticRank;
                hit.similarity = vectorHit.similarity;
                continue;
            }

            HybridHit hit;
            hit.chunkId = vectorHit.sourceId;
            hit.semanticRank = semanticRank;
            hit.similarity = vectorHit.similarity;
            hits.insert(hit.chunkId, hit);
            missing.append(hit.chunkId);
        }

        // Read the live chunk for vector-only hits: current path, lines and
        // text, and only while the chunk is still searchable in this Library.
        for (qsizetype start = 0; start < missing.size(); start += 200) {
            const QStringList slice = missing.mid(start, 200);
            auto statement = database.prepare(QStringLiteral(
                "SELECT c.id AS chunkId, c.library_id AS libraryId, c.library_file_id AS fileId, f.relative_path AS relativePath, "
                "f.name AS fileName, c.content AS content, c.content_hash AS contentHash, c.start_line AS startLine, c.end_line AS endLine "
                "FROM library_chunks c INNER JOIN library_files f ON f.id = c.library_file_id "
                "INNER JOIN library_documents d ON d.library_file_id = c.library_file_id "
                "WHERE c.library_id = ? AND d.status = 'indexed' AND f.status = 'available' AND c.id IN (%1)"
            ).arg(CoreValues::placeholders(slice.size())));
            statement.bind(1, libraryId);

            for (qsizetype index = 0; index < slice.size(); ++index) {
                statement.bind(static_cast<int>(index) + 2, slice.at(index));
            }

            QSet<QString> found;

            for (const QVariantMap &row : statement.all()) {
                HybridHit &hit = hits[row.value(QStringLiteral("chunkId")).toString()];
                hit.libraryId = row.value(QStringLiteral("libraryId")).toString();
                hit.fileId = row.value(QStringLiteral("fileId")).toString();
                hit.relativePath = row.value(QStringLiteral("relativePath")).toString();
                hit.fileName = row.value(QStringLiteral("fileName")).toString();
                hit.content = row.value(QStringLiteral("content")).toString();
                hit.contentHash = row.value(QStringLiteral("contentHash")).toString();
                hit.startLine = row.value(QStringLiteral("startLine")).toInt();
                hit.endLine = row.value(QStringLiteral("endLine")).toInt();
                found.insert(hit.chunkId);
                order.append(hit.chunkId);
            }

            for (const QString &id : slice) {
                if (!found.contains(id)) {
                    hits.remove(id);
                }
            }
        }
    }

    // 3. Exact matches and fusion.
    const QStringList terms = exactTerms(query);
    QList<FusionInput> inputs;

    for (const QString &id : std::as_const(order)) {
        HybridHit &hit = hits[id];
        hit.exact = terms.isEmpty() ? ExactMatch::None : exactMatch(terms, hit.relativePath, hit.fileName, hit.content);
        inputs.append({id, hit.lexicalRank, hit.semanticRank, hit.exact});
    }

    for (const FusedRank &rank : fuse(inputs, policy)) {
        if (result.hits.size() >= clampedLimit) {
            break;
        }

        HybridHit hit = hits.value(rank.id);
        hit.score = std::round(rank.score * 1e9) / 1e9;
        hit.reason = hitReason(hit);
        result.hits.append(hit);
    }

    result.diagnostics.insert(QStringLiteral("mode"), semantic ? QStringLiteral("hybrid") : QStringLiteral("lexical"));
    result.diagnostics.insert(QStringLiteral("store"), store.name());
    result.diagnostics.insert(QStringLiteral("lexicalCandidates"), lexicalRank);
    result.diagnostics.insert(QStringLiteral("semanticCandidates"), semanticCandidates);
    result.diagnostics.insert(QStringLiteral("exactTerms"), terms.size());
    result.diagnostics.insert(QStringLiteral("lexicalMs"), lexicalMs);
    result.diagnostics.insert(QStringLiteral("vectorMs"), vectorMs);
    result.diagnostics.insert(QStringLiteral("totalMs"), milliseconds(total));
    live.setField(QStringLiteral("mode"), semantic ? QStringLiteral("hybrid") : QStringLiteral("lexical"));
    live.setField(QStringLiteral("vectorsScanned"), result.diagnostics.value(QStringLiteral("vectorsScanned"), 0));
    live.setField(QStringLiteral("hits"), result.hits.size());
    return result;
}
}
