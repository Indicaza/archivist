#include "knowledge_retrieval.h"

#include "knowledge_text.h"
#include "knowledge_types.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Knowledge;

namespace
{
QString ftsQuery(const QString &query)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}_]+"));
    static const QSet<QString> ignored{
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("are"), QStringLiteral("was"),
        QStringLiteral("what"), QStringLiteral("how"), QStringLiteral("why"), QStringLiteral("who"), QStringLiteral("does"),
        QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("about"),
        QStringLiteral("is"), QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("in"), QStringLiteral("on"),
        QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("it"), QStringLiteral("do"), QStringLiteral("we"),
        QStringLiteral("our"), QStringLiteral("tell"), QStringLiteral("me"), QStringLiteral("please"),
    };
    QStringList terms;
    QSet<QString> seen;
    auto iterator = word.globalMatch(query.toLower());

    while (iterator.hasNext() && terms.size() < 24) {
        const QString token = iterator.next().captured(0);

        if (token.size() < 2 || ignored.contains(token) || seen.contains(token)) {
            continue;
        }

        seen.insert(token);
        terms.append(QLatin1Char('"') + QString(token).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'));
    }

    return terms.join(QStringLiteral(" OR "));
}

QString key(const QString &type, const QString &id)
{
    return type + QLatin1Char(':') + id;
}

// The record behind a search key, if it is still searchable.
std::optional<KnowledgeRetrieval::Hit> load(CoreDatabase &database, const QString &libraryId, const QString &type, const QString &id)
{
    KnowledgeRetrieval::Hit hit;
    hit.entryType = type;
    hit.entryId = id;

    if (type == QStringLiteral("claim")) {
        const auto row = database
                             .prepare(QStringLiteral(
                                 "SELECT k.library_id AS libraryId, k.page_id AS pageId, p.title AS pageTitle, p.slug AS pageSlug, k.statement AS text, "
                                 "k.claim_type AS claimType, k.verification_status AS status, k.confidence, p.review_status AS reviewStatus, "
                                 "k.updated_at AS updatedAt, p.reconciled_at AS reconciledAt, k.content_hash AS contentHash "
                                 "FROM knowledge_claims k INNER JOIN knowledge_pages p ON p.id = k.page_id "
                                 "WHERE k.id = ? AND k.library_id = ? AND p.status = 'active' "
                                 "AND k.verification_status NOT IN ('retracted', 'stale')"
                             ))
                             .bind(1, id)
                             .bind(2, libraryId)
                             .one();

        if (!row) {
            return std::nullopt;
        }

        hit.libraryId = row->value(QStringLiteral("libraryId")).toString();
        hit.pageId = row->value(QStringLiteral("pageId")).toString();
        hit.pageTitle = row->value(QStringLiteral("pageTitle")).toString();
        hit.pageSlug = row->value(QStringLiteral("pageSlug")).toString();
        hit.text = row->value(QStringLiteral("text")).toString();
        hit.claimType = row->value(QStringLiteral("claimType")).toString();
        hit.verificationStatus = row->value(QStringLiteral("status")).toString();
        hit.confidence = row->value(QStringLiteral("confidence")).toDouble();
        hit.reviewStatus = row->value(QStringLiteral("reviewStatus")).toString();
        hit.updatedAt = row->value(QStringLiteral("updatedAt")).toString();
        hit.reconciledAt = row->value(QStringLiteral("reconciledAt")).toString();
        QSet<QString> passages;

        for (const QVariantMap &citation : database
                                               .prepare(QStringLiteral(
                                                   "SELECT relative_path AS path, start_line AS startLine, end_line AS endLine, chunk_hash AS hash "
                                                   "FROM knowledge_citations WHERE claim_id = ? AND status IN ('valid', 'moved', 'reanchored') "
                                                   "ORDER BY relative_path, start_line"
                                               ))
                                               .bind(1, id)
                                               .all()) {
            passages.insert(citation.value(QStringLiteral("hash")).toString());
            const int start = citation.value(QStringLiteral("startLine")).toInt();
            const int end = citation.value(QStringLiteral("endLine")).toInt();
            const QString label = citation.value(QStringLiteral("path")).toString()
                + (start > 0 ? QStringLiteral(":%1%2").arg(start).arg(end > start ? QStringLiteral("-%1").arg(end) : QString()) : QString());

            if (!hit.sourceLabels.contains(label)) {
                hit.sourceLabels.append(label);
            }
        }

        hit.supportingPassages = static_cast<int>(passages.size());
        return hit;
    }

    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT library_id AS libraryId, title, slug, content, review_status AS reviewStatus, updated_at AS updatedAt, "
                             "reconciled_at AS reconciledAt FROM knowledge_pages WHERE id = ? AND library_id = ? AND status = 'active'"
                         ))
                         .bind(1, id)
                         .bind(2, libraryId)
                         .one();

    if (!row) {
        return std::nullopt;
    }

    hit.libraryId = row->value(QStringLiteral("libraryId")).toString();
    hit.pageId = id;
    hit.pageTitle = row->value(QStringLiteral("title")).toString();
    hit.pageSlug = row->value(QStringLiteral("slug")).toString();
    hit.text = row->value(QStringLiteral("content")).toString();
    hit.reviewStatus = row->value(QStringLiteral("reviewStatus")).toString();
    hit.updatedAt = row->value(QStringLiteral("updatedAt")).toString();
    hit.reconciledAt = row->value(QStringLiteral("reconciledAt")).toString();
    // A page is as trustworthy as the claims standing on it, on average.
    const auto stats = database
                           .prepare(QStringLiteral(
                               "SELECT AVG(confidence) AS confidence, COUNT(*) AS claims FROM knowledge_claims "
                               "WHERE page_id = ? AND verification_status NOT IN ('retracted')"
                           ))
                           .bind(1, id)
                           .one();
    hit.confidence = stats && stats->value(QStringLiteral("claims")).toInt() > 0 ? stats->value(QStringLiteral("confidence")).toDouble() : 0.25;
    hit.verificationStatus = hit.reviewStatus == ReviewStatus::needsReview ? QStringLiteral("needs-review") : QStringLiteral("reviewed");
    return hit;
}
}

namespace KnowledgeRetrieval
{
bool hasKnowledge(CoreDatabase &database, const QString &libraryId)
{
    return database.prepare(QStringLiteral("SELECT 1 FROM knowledge_pages WHERE library_id = ? AND status = 'active' LIMIT 1")).bind(1, libraryId).scalar().isValid();
}

Result search(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &query,
    const QVector<float> &queryVector,
    int limit,
    const Retrieval::HybridPolicy &policy,
    const Retrieval::EmbeddingConfig &config,
    const Retrieval::VectorStore &store
)
{
    QElapsedTimer timer;
    timer.start();
    Result result;
    QHash<QString, int> lexicalRank;
    QHash<QString, int> semanticRank;
    QHash<QString, double> similarity;
    QStringList order;

    // 1. Keywords.
    const QString fts = ftsQuery(query);

    if (!fts.isEmpty()) {
        const auto rows = database
                              .prepare(QStringLiteral(
                                  "SELECT s.entry_type AS type, s.entry_id AS id FROM knowledge_search s "
                                  "WHERE knowledge_search MATCH ? AND s.library_id = ? ORDER BY bm25(knowledge_search, 0.0, 0.0, 0.0, 0.0, 2.0, 1.0) ASC, s.entry_id ASC LIMIT ?"
                              ))
                              .bind(1, fts)
                              .bind(2, libraryId)
                              .bind(3, policy.lexicalLimit * 2)
                              .all();
        int rank = 0;

        for (const QVariantMap &row : rows) {
            const QString id = key(row.value(QStringLiteral("type")).toString(), row.value(QStringLiteral("id")).toString());

            if (!lexicalRank.contains(id)) {
                lexicalRank.insert(id, ++rank);
                order.append(id);
            }
        }
    }

    // 2. Meaning, over the same shared vectors as Library chunks.
    int scanned = 0;
    const bool semantic = policy.semanticEnabled && !queryVector.isEmpty();

    if (semantic) {
        QList<Retrieval::VectorHit> vectorHits;

        for (const QString &sourceType : {Retrieval::SourceType::wikiClaim, Retrieval::SourceType::wikiPage}) {
            Retrieval::VectorQuery vectorQuery;
            vectorQuery.vector = queryVector;
            vectorQuery.config = config;
            vectorQuery.sourceType = sourceType;
            vectorQuery.scopeType = Retrieval::ScopeType::library;
            vectorQuery.scopeIds = {libraryId};
            vectorQuery.limit = policy.semanticLimit;
            vectorQuery.minSimilarity = policy.minSimilarity;
            vectorQuery.maxScanned = policy.maxScannedVectors;
            const Retrieval::VectorSearchResult found = store.search(database, vectorQuery);
            scanned += found.scanned;
            vectorHits.append(found.hits);
        }

        std::sort(vectorHits.begin(), vectorHits.end(), [](const auto &a, const auto &b) {
            return a.similarity != b.similarity ? a.similarity > b.similarity : a.sourceId < b.sourceId;
        });
        int rank = 0;

        for (const Retrieval::VectorHit &hit : std::as_const(vectorHits)) {
            const QString type = hit.sourceType == Retrieval::SourceType::wikiClaim ? QStringLiteral("claim") : QStringLiteral("page");
            const QString id = key(type, hit.sourceId);

            if (!semanticRank.contains(id)) {
                semanticRank.insert(id, ++rank);
                similarity.insert(id, hit.similarity);

                if (!lexicalRank.contains(id)) {
                    order.append(id);
                }
            }
        }
    }

    // 3. Fuse, then weigh by how well each record is supported.
    QList<Retrieval::FusionInput> inputs;

    for (const QString &id : std::as_const(order)) {
        inputs.append({id, lexicalRank.value(id), semanticRank.value(id), Retrieval::ExactMatch::None});
    }

    const QList<Retrieval::FusedRank> fused = Retrieval::fuse(inputs, policy);
    QList<Hit> hits;

    for (const Retrieval::FusedRank &rank : fused) {
        const qsizetype colon = rank.id.indexOf(QLatin1Char(':'));
        auto hit = load(database, libraryId, rank.id.left(colon), rank.id.mid(colon + 1));

        if (!hit) {
            continue;
        }

        hit->lexicalRank = rank.lexicalRank;
        hit->semanticRank = rank.semanticRank;
        hit->similarity = similarity.value(rank.id);
        // Retrieval ranks relevance; confidence and review state temper it,
        // so a weakly supported interpretation does not outrank a strong one.
        const double support = 0.5 + 0.5 * std::clamp(hit->confidence, 0.0, 1.0);
        const double freshness = hit->reviewStatus == ReviewStatus::needsReview ? 0.7 : 1.0;
        hit->score = std::round(rank.score * support * freshness * 1e9) / 1e9;
        QStringList found;

        if (rank.lexicalRank > 0) {
            found.append(QStringLiteral("keyword #%1").arg(rank.lexicalRank));
        }

        if (rank.semanticRank > 0) {
            found.append(QStringLiteral("semantic #%1 (%2)").arg(rank.semanticRank).arg(hit->similarity, 0, 'f', 2));
        }

        hit->reason = QStringLiteral("Wiki %1: %2; confidence %3").arg(hit->entryType, found.join(QStringLiteral(", "))).arg(hit->confidence, 0, 'f', 2);
        hits.append(*hit);
    }

    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }

        return key(a.entryType, a.entryId) < key(b.entryType, b.entryId);
    });

    result.hits = hits.mid(0, std::max(0, limit));
    result.diagnostics = {
        {QStringLiteral("mode"), semantic ? QStringLiteral("hybrid") : QStringLiteral("lexical")},
        {QStringLiteral("lexicalCandidates"), lexicalRank.size()},
        {QStringLiteral("semanticCandidates"), semanticRank.size()},
        {QStringLiteral("vectorsScanned"), scanned},
        {QStringLiteral("totalMs"), std::round(static_cast<double>(timer.nsecsElapsed()) / 1000.0) / 1000.0},
    };
    return result;
}

QString contextText(const Hit &hit)
{
    if (hit.entryType == QStringLiteral("claim")) {
        QString text = QStringLiteral("Wiki claim on “%1” (%2, %3, confidence %4). ")
                           .arg(hit.pageTitle, hit.claimType, hit.verificationStatus)
                           .arg(hit.confidence, 0, 'f', 2);
        text += hit.text;

        if (!hit.sourceLabels.isEmpty()) {
            text += QStringLiteral("\nOriginal sources: ") + hit.sourceLabels.join(QStringLiteral("; "));
        } else {
            text += QStringLiteral("\nOriginal sources: none (not established by the Library).");
        }

        return text;
    }

    return QStringLiteral("Wiki page “%1” (%2, confidence %3). AI-maintained summary; check the original sources it cites before relying on it.\n%4")
        .arg(hit.pageTitle, hit.verificationStatus)
        .arg(hit.confidence, 0, 'f', 2)
        .arg(hit.text.left(2400));
}
}
