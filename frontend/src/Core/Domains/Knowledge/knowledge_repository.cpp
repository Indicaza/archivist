#include "knowledge_repository.h"

#include "knowledge_text.h"
#include "knowledge_types.h"

#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <QSet>

#include <algorithm>

using namespace CoreValues;
using namespace Knowledge;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString pageColumns = QStringLiteral(
    "p.id, p.library_id AS libraryId, p.slug, p.title, p.page_type AS pageType, p.status, "
    "p.review_status AS reviewStatus, p.revision, p.origin, p.manually_edited_at AS manuallyEditedAt, "
    "p.merged_into_page_id AS mergedIntoPageId, p.reconciled_at AS reconciledAt, p.content_hash AS contentHash, "
    "p.created_at AS createdAt, p.updated_at AS updatedAt"
);

const QString claimColumns = QStringLiteral(
    "k.id, k.library_id AS libraryId, k.page_id AS pageId, k.claim_type AS claimType, k.statement, "
    "k.position, k.model_confidence AS modelConfidence, k.confidence, k.verification_status AS verificationStatus, "
    "k.status_reason AS statusReason, k.origin, k.verified_at AS verifiedAt, k.created_at AS createdAt, "
    "k.updated_at AS updatedAt"
);

const QString citationColumns = QStringLiteral(
    "id, claim_id AS claimId, source_type AS sourceType, library_file_id AS fileId, relative_path AS relativePath, "
    "chunk_id AS chunkId, chunk_hash AS chunkHash, quote, start_line AS startLine, end_line AS endLine, "
    "file_content_hash AS fileContentHash, producer_chat_id AS producerChatId, producer_agent_id AS producerAgentId, "
    "producer_run_id AS producerRunId, producer_job_id AS producerJobId, status, status_detail AS statusDetail, "
    "checked_at AS checkedAt, created_at AS createdAt"
);

QVariantMap nullableMap(const QVariantMap &row)
{
    QVariantMap map;

    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        map.insert(it.key(), nullable(it.value()));
    }

    return map;
}

QVariant optionalText(const QString &value)
{
    return value.isEmpty() ? QVariant() : QVariant(value);
}

QVariant optionalLine(int value)
{
    return value > 0 ? QVariant(value) : QVariant();
}

QVariantMap withVirtualPath(QVariantMap page)
{
    page.insert(QStringLiteral("virtualPath"), KnowledgeRepository::virtualPath(page.value(QStringLiteral("slug")).toString()));
    return page;
}

QString recordLibrary(CoreDatabase &database, const QString &type, const QString &id)
{
    const QString table = type == QStringLiteral("claim") ? QStringLiteral("knowledge_claims")
        : type == QStringLiteral("page")                  ? QStringLiteral("knowledge_pages")
                                                          : QString();

    if (table.isEmpty()) {
        throw CoreError(QStringLiteral("Unknown Wiki record type: %1").arg(type));
    }

    return database.prepare(QStringLiteral("SELECT library_id FROM %1 WHERE id = ?").arg(table)).bind(1, id).scalar().toString();
}

QString recordLabel(CoreDatabase &database, const QString &type, const QString &id)
{
    if (type == QStringLiteral("page")) {
        return database.prepare(QStringLiteral("SELECT title FROM knowledge_pages WHERE id = ?")).bind(1, id).scalar().toString();
    }

    return database.prepare(QStringLiteral("SELECT statement FROM knowledge_claims WHERE id = ?")).bind(1, id).scalar().toString();
}
}

namespace KnowledgeRepository
{
QString virtualPath(const QString &slug)
{
    return QStringLiteral("Wiki/%1.md").arg(slug);
}

QVariantList pages(CoreDatabase &database, const QString &libraryId, bool includeInactive)
{
    QVariantList result;
    const auto rows = database
                          .prepare(QStringLiteral(
                                       "SELECT %1, "
                                       "(SELECT COUNT(*) FROM knowledge_claims k WHERE k.page_id = p.id AND k.verification_status != 'retracted') AS claimCount, "
                                       "(SELECT COUNT(*) FROM knowledge_claims k WHERE k.page_id = p.id AND k.verification_status = 'stale') AS staleClaimCount, "
                                       "(SELECT COUNT(*) FROM knowledge_claims k WHERE k.page_id = p.id AND k.verification_status = 'contested') AS contestedClaimCount, "
                                       "(SELECT COUNT(*) FROM knowledge_proposals x WHERE x.target_page_id = p.id AND x.status = 'pending') AS pendingProposalCount "
                                       "FROM knowledge_pages p WHERE p.library_id = ? %2 ORDER BY p.title COLLATE NOCASE ASC, p.id ASC"
                                   )
                                       .arg(pageColumns, includeInactive ? QString() : QStringLiteral("AND p.status = 'active'")))
                          .bind(1, libraryId)
                          .all();

    for (const QVariantMap &row : rows) {
        result.append(withVirtualPath(nullableMap(row)));
    }

    return result;
}

std::optional<QVariantMap> pageRow(CoreDatabase &database, const QString &pageId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1, p.content FROM knowledge_pages p WHERE p.id = ?").arg(pageColumns)).bind(1, pageId).one();
    return row ? std::optional<QVariantMap>(withVirtualPath(nullableMap(*row))) : std::nullopt;
}

std::optional<QVariantMap> pageBySlug(CoreDatabase &database, const QString &libraryId, const QString &slug)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1, p.content FROM knowledge_pages p WHERE p.library_id = ? AND p.slug = ?").arg(pageColumns))
                         .bind(1, libraryId)
                         .bind(2, slug)
                         .one();
    return row ? std::optional<QVariantMap>(withVirtualPath(nullableMap(*row))) : std::nullopt;
}

std::optional<QVariantMap> pageDetail(CoreDatabase &database, const QString &pageId)
{
    auto page = pageRow(database, pageId);

    if (!page) {
        return std::nullopt;
    }

    QVariantList claimList;

    for (const QVariant &value : claims(database, pageId, true)) {
        QVariantMap item = value.toMap();
        const QString claimId = item.value(QStringLiteral("id")).toString();
        item.insert(QStringLiteral("citations"), citations(database, claimId));
        item.insert(QStringLiteral("relations"), relations(database, QStringLiteral("claim"), claimId));
        claimList.append(item);
    }

    page->insert(QStringLiteral("claims"), claimList);
    page->insert(QStringLiteral("relations"), relations(database, QStringLiteral("page"), pageId));
    page->insert(QStringLiteral("revisions"), revisions(database, pageId));
    return page;
}

QString uniqueSlug(CoreDatabase &database, const QString &libraryId, const QString &title)
{
    const QString base = KnowledgeText::slugify(title);
    QString slug = base;

    for (int suffix = 2; database.prepare(QStringLiteral("SELECT 1 FROM knowledge_pages WHERE library_id = ? AND slug = ?"))
                             .bind(1, libraryId)
                             .bind(2, slug)
                             .scalar()
                             .isValid();
         ++suffix) {
        slug = QStringLiteral("%1-%2").arg(base).arg(suffix);
    }

    return slug;
}

QString createPage(CoreDatabase &database, const PageDraft &draft)
{
    if (draft.title.trimmed().isEmpty()) {
        throw CoreError(QStringLiteral("A Wiki page needs a title."));
    }

    if (!PageType::all().contains(draft.pageType)) {
        throw CoreError(QStringLiteral("Unknown Wiki page type: %1").arg(draft.pageType));
    }

    return Transactions::join(database, [&]() {
        const QString id = newId();
        const QString slug = draft.slug.isEmpty() ? uniqueSlug(database, draft.libraryId, draft.title) : draft.slug;
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_pages (id, library_id, slug, title, page_type, content, content_hash, status, "
                "review_status, revision, origin) VALUES (?, ?, ?, ?, ?, ?, ?, 'active', 'reviewed', 1, ?)"
            ))
            .bind(1, id)
            .bind(2, draft.libraryId)
            .bind(3, slug)
            .bind(4, draft.title.trimmed())
            .bind(5, draft.pageType)
            .bind(6, draft.content)
            .bind(7, KnowledgeText::pageContentHash(draft.title, draft.content))
            .bind(8, draft.origin.isEmpty() ? Origin::ai : draft.origin)
            .run();
        return id;
    });
}

void updatePageFields(CoreDatabase &database, const QString &pageId, const QString &title, const QString &pageType, const QString &content)
{
    if (title.trimmed().isEmpty()) {
        throw CoreError(QStringLiteral("A Wiki page needs a title."));
    }

    if (!PageType::all().contains(pageType)) {
        throw CoreError(QStringLiteral("Unknown Wiki page type: %1").arg(pageType));
    }

    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_pages SET title = ?, page_type = ?, content = ?, content_hash = ?, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, title.trimmed())
        .bind(2, pageType)
        .bind(3, content)
        .bind(4, KnowledgeText::pageContentHash(title, content))
        .bind(5, pageId)
        .run();
}

void setPageStatus(CoreDatabase &database, const QString &pageId, const QString &status, const QString &mergedInto)
{
    database.prepare(QStringLiteral("UPDATE knowledge_pages SET status = ?, merged_into_page_id = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, status)
        .bind(2, optionalText(mergedInto))
        .bind(3, pageId)
        .run();
}

void setReviewStatus(CoreDatabase &database, const QString &pageId, const QString &reviewStatus)
{
    database.prepare(QStringLiteral("UPDATE knowledge_pages SET review_status = ?, updated_at = %1 WHERE id = ? AND review_status != ?").arg(nowSql))
        .bind(1, reviewStatus)
        .bind(2, pageId)
        .bind(3, reviewStatus)
        .run();
}

void markManuallyEdited(CoreDatabase &database, const QString &pageId)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_pages SET manually_edited_at = %1, "
                     "origin = CASE WHEN origin = 'manual' THEN 'manual' ELSE 'mixed' END, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, pageId)
        .run();
}

void touchReconciled(CoreDatabase &database, const QString &pageId)
{
    database.prepare(QStringLiteral("UPDATE knowledge_pages SET reconciled_at = %1 WHERE id = ?").arg(nowSql)).bind(1, pageId).run();
}

int commitRevision(CoreDatabase &database, const QString &pageId, const QString &changeKind, const ActorRef &actor, const QString &proposalId, const QString &note)
{
    return Transactions::join(database, [&]() {
        const auto page = pageRow(database, pageId);

        if (!page) {
            throw CoreError(QStringLiteral("The Wiki page no longer exists."));
        }

        const int current = page->value(QStringLiteral("revision")).toInt();
        const QString currentId = database.prepare(QStringLiteral("SELECT id FROM knowledge_revisions WHERE page_id = ? AND revision = ?"))
                                      .bind(1, pageId)
                                      .bind(2, current)
                                      .scalar()
                                      .toString();
        // A new page has no revision row yet: record revision 1 as is.
        const bool first = currentId.isEmpty();
        const int next = first ? current : current + 1;
        const QString previousId = first ? QString() : currentId;
        QVariantList snapshot;

        for (const QVariant &value : claims(database, pageId, true)) {
            const QVariantMap item = value.toMap();
            snapshot.append(QVariantMap{
                {QStringLiteral("id"), item.value(QStringLiteral("id"))},
                {QStringLiteral("claimType"), item.value(QStringLiteral("claimType"))},
                {QStringLiteral("statement"), item.value(QStringLiteral("statement"))},
                {QStringLiteral("verificationStatus"), item.value(QStringLiteral("verificationStatus"))},
                {QStringLiteral("confidence"), item.value(QStringLiteral("confidence"))},
                {QStringLiteral("origin"), item.value(QStringLiteral("origin"))},
                {QStringLiteral("position"), item.value(QStringLiteral("position"))},
            });
        }

        database.prepare(QStringLiteral("UPDATE knowledge_pages SET revision = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
            .bind(1, next)
            .bind(2, pageId)
            .run();
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_revisions (id, library_id, page_id, revision, previous_revision_id, title, page_type, "
                "status, content, content_hash, claims, change_kind, actor_type, actor_id, proposal_id, note) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            ))
            .bind(1, newId())
            .bind(2, page->value(QStringLiteral("libraryId")))
            .bind(3, pageId)
            .bind(4, next)
            .bind(5, optionalText(previousId))
            .bind(6, page->value(QStringLiteral("title")))
            .bind(7, page->value(QStringLiteral("pageType")))
            .bind(8, page->value(QStringLiteral("status")))
            .bind(9, page->value(QStringLiteral("content")))
            .bind(10, page->value(QStringLiteral("contentHash")))
            .bind(11, JsonValues::toJsonString(snapshot))
            .bind(12, changeKind)
            .bind(13, actor.type)
            .bind(14, optionalText(actor.id))
            .bind(15, optionalText(proposalId))
            .bind(16, optionalText(note))
            .run();
        return next;
    });
}

QVariantList revisions(CoreDatabase &database, const QString &pageId)
{
    QVariantList result;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                          "SELECT id, page_id AS pageId, revision, previous_revision_id AS previousRevisionId, title, "
                                          "page_type AS pageType, status, change_kind AS changeKind, actor_type AS actorType, "
                                          "actor_id AS actorId, proposal_id AS proposalId, note, created_at AS createdAt "
                                          "FROM knowledge_revisions WHERE page_id = ? ORDER BY revision DESC"
                                      ))
                                      .bind(1, pageId)
                                      .all()) {
        result.append(nullableMap(row));
    }

    return result;
}

std::optional<QVariantMap> revision(CoreDatabase &database, const QString &revisionId)
{
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT id, library_id AS libraryId, page_id AS pageId, revision, previous_revision_id AS previousRevisionId, "
                             "title, page_type AS pageType, status, content, content_hash AS contentHash, claims, "
                             "change_kind AS changeKind, actor_type AS actorType, actor_id AS actorId, proposal_id AS proposalId, "
                             "note, created_at AS createdAt FROM knowledge_revisions WHERE id = ?"
                         ))
                         .bind(1, revisionId)
                         .one();

    if (!row) {
        return std::nullopt;
    }

    QVariantMap result = nullableMap(*row);
    result.insert(QStringLiteral("claims"), JsonValues::parse(row->value(QStringLiteral("claims")).toString()).toList());
    return result;
}

double confidenceFor(const QString &status, const QString &claimType, const QString &origin, int supportingPassages, double modelConfidence)
{
    // The model's own number can only lower a score, never raise it.
    const double model = modelConfidence < 0 ? 1.0 : std::clamp(modelConfidence, 0.0, 1.0);
    double score = 0;

    if (status == Verification::retracted) {
        return 0;
    }

    if (status == Verification::verified) {
        score = 0.95;
    } else if (status == Verification::stale) {
        score = 0.15;
    } else if (status == Verification::contested) {
        score = 0.3;
    } else if (status == Verification::supported) {
        // One passage: 0.6; each further distinct passage +0.1; capped at
        // 0.85. Only a person can take a claim above that.
        score = std::min(0.85, 0.6 + 0.1 * std::max(0, supportingPassages - 1));
    } else if (!ClaimType::requiresEvidence(claimType)) {
        score = 0.3; // questions and assumptions: open by nature
    } else {
        score = origin == Origin::manual ? 0.5 : 0.25;
    }

    return std::round(std::min(score, status == Verification::verified ? score : model) * 100.0) / 100.0;
}

QString refreshClaimSupport(CoreDatabase &database, const QString &claimId)
{
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT claim_type AS claimType, origin, verification_status AS status, model_confidence AS modelConfidence, "
                             "status_reason AS reason FROM knowledge_claims WHERE id = ?"
                         ))
                         .bind(1, claimId)
                         .one();

    if (!row) {
        return {};
    }

    const QString current = row->value(QStringLiteral("status")).toString();
    const auto counts = database
                            .prepare(QStringLiteral(
                                "SELECT COUNT(*) AS total, "
                                "COUNT(DISTINCT CASE WHEN status IN ('valid', 'moved', 'reanchored') THEN chunk_hash || ':' || quote_key END) AS supporting, "
                                "group_concat(DISTINCT CASE WHEN status NOT IN ('valid', 'moved', 'reanchored') THEN status END) AS problems "
                                "FROM knowledge_citations WHERE claim_id = ?"
                            ))
                            .bind(1, claimId)
                            .one();
    const int total = counts->value(QStringLiteral("total")).toInt();
    // Distinct passages, so citing one passage twice is one source.
    const int supportingPassages = database
                                       .prepare(QStringLiteral(
                                           "SELECT COUNT(DISTINCT chunk_hash) FROM knowledge_citations WHERE claim_id = ? "
                                           "AND status IN ('valid', 'moved', 'reanchored')"
                                       ))
                                       .bind(1, claimId)
                                       .scalar()
                                       .toInt();
    QString status = current;
    QString reason = row->value(QStringLiteral("reason")).toString();

    if (current != Verification::retracted) {
        if (total > 0 && supportingPassages == 0) {
            status = Verification::stale;
            reason = QStringLiteral("Its cited sources changed: %1.").arg(counts->value(QStringLiteral("problems")).toString());
        } else if (current == Verification::verified || current == Verification::contested) {
            // A person's verdict or an open contradiction stays until a person resolves it.
            if (current == Verification::verified) {
                reason.clear();
            }
        } else if (supportingPassages > 0) {
            status = Verification::supported;
            reason.clear();
        } else {
            status = Verification::unverified;
            reason = ClaimType::requiresEvidence(row->value(QStringLiteral("claimType")).toString())
                ? QStringLiteral("No original source supports it.")
                : QString();
        }
    }

    const QVariant modelValue = row->value(QStringLiteral("modelConfidence"));
    const double confidence = confidenceFor(
        status,
        row->value(QStringLiteral("claimType")).toString(),
        row->value(QStringLiteral("origin")).toString(),
        supportingPassages,
        modelValue.isNull() ? -1 : modelValue.toDouble()
    );
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_claims SET verification_status = ?, status_reason = ?, confidence = ?, updated_at = %1 "
                     "WHERE id = ? AND (verification_status != ? OR IFNULL(status_reason, '') != ? OR confidence != ?)"
        )
                     .arg(nowSql))
        .bind(1, status)
        .bind(2, optionalText(reason))
        .bind(3, confidence)
        .bind(4, claimId)
        .bind(5, status)
        .bind(6, reason)
        .bind(7, confidence)
        .run();
    return status;
}

bool addCitation(CoreDatabase &database, const QString &libraryId, const QString &claimId, const CitationDraft &citation)
{
    if (citation.chunkHash.isEmpty() || citation.relativePath.isEmpty()) {
        throw CoreError(QStringLiteral("A citation needs the cited passage and its file."));
    }

    database
        .prepare(QStringLiteral(
                     "INSERT INTO knowledge_citations (id, library_id, claim_id, source_type, library_file_id, relative_path, chunk_id, "
                     "chunk_hash, quote, quote_key, start_line, end_line, file_content_hash, producer_chat_id, producer_agent_id, "
                     "producer_run_id, producer_job_id, status, checked_at) "
                     "VALUES (?, ?, ?, 'library-chunk', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'valid', %1) "
                     "ON CONFLICT(claim_id, chunk_hash, quote_key) DO NOTHING"
        )
                     .arg(nowSql))
        .bind(1, newId())
        .bind(2, libraryId)
        .bind(3, claimId)
        .bind(4, optionalText(citation.fileId))
        .bind(5, citation.relativePath)
        .bind(6, optionalText(citation.chunkId))
        .bind(7, citation.chunkHash)
        .bind(8, citation.quote)
        .bind(9, KnowledgeText::quoteKey(citation.quote))
        .bind(10, optionalLine(citation.startLine))
        .bind(11, optionalLine(citation.endLine))
        .bind(12, optionalText(citation.fileContentHash))
        .bind(13, optionalText(citation.producerChatId))
        .bind(14, optionalText(citation.producerAgentId))
        .bind(15, optionalText(citation.producerRunId))
        .bind(16, optionalText(citation.producerJobId))
        .run();
    return database.changes() == 1;
}

ClaimWrite upsertClaim(CoreDatabase &database, const QString &libraryId, const QString &pageId, const ClaimDraft &draft)
{
    if (!ClaimType::all().contains(draft.type)) {
        throw CoreError(QStringLiteral("Unknown claim type: %1").arg(draft.type));
    }

    const QString statement = draft.statement.simplified();

    if (statement.isEmpty()) {
        throw CoreError(QStringLiteral("A claim needs a statement."));
    }

    return Transactions::join(database, [&]() {
        ClaimWrite write;
        const QString key = KnowledgeText::statementKey(statement);
        const auto existing = database
                                  .prepare(QStringLiteral("SELECT id, verification_status AS status FROM knowledge_claims WHERE page_id = ? AND statement_key = ?"))
                                  .bind(1, pageId)
                                  .bind(2, key)
                                  .one();

        if (existing) {
            write.claimId = existing->value(QStringLiteral("id")).toString();

            if (existing->value(QStringLiteral("status")).toString() == Verification::retracted) {
                database.prepare(QStringLiteral("UPDATE knowledge_claims SET verification_status = 'unverified', status_reason = NULL, position = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
                    .bind(1, draft.position)
                    .bind(2, write.claimId)
                    .run();
            }
        } else {
            write.claimId = newId();
            write.created = true;
            database
                .prepare(QStringLiteral(
                    "INSERT INTO knowledge_claims (id, library_id, page_id, claim_type, statement, statement_key, content_hash, "
                    "position, model_confidence, confidence, verification_status, origin) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 'unverified', ?)"
                ))
                .bind(1, write.claimId)
                .bind(2, libraryId)
                .bind(3, pageId)
                .bind(4, draft.type)
                .bind(5, statement)
                .bind(6, key)
                .bind(7, KnowledgeText::sha256(statement))
                .bind(8, draft.position)
                .bind(9, draft.modelConfidence < 0 ? QVariant() : QVariant(std::clamp(draft.modelConfidence, 0.0, 1.0)))
                .bind(10, draft.origin.isEmpty() ? Origin::ai : draft.origin)
                .run();
        }

        for (const CitationDraft &citation : draft.citations) {
            if (addCitation(database, libraryId, write.claimId, citation)) {
                ++write.citationsAdded;
            }
        }

        refreshClaimSupport(database, write.claimId);
        return write;
    });
}

std::optional<QVariantMap> claim(CoreDatabase &database, const QString &claimId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM knowledge_claims k WHERE k.id = ?").arg(claimColumns)).bind(1, claimId).one();
    return row ? std::optional<QVariantMap>(nullableMap(*row)) : std::nullopt;
}

QVariantList claims(CoreDatabase &database, const QString &pageId, bool includeRetracted)
{
    QVariantList result;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                          "SELECT %1, (SELECT COUNT(*) FROM knowledge_citations c WHERE c.claim_id = k.id) AS citationCount "
                                          "FROM knowledge_claims k WHERE k.page_id = ? %2 ORDER BY k.position ASC, k.created_at ASC, k.id ASC"
                                      )
                                                   .arg(claimColumns, includeRetracted ? QString() : QStringLiteral("AND k.verification_status != 'retracted'")))
                                      .bind(1, pageId)
                                      .all()) {
        result.append(nullableMap(row));
    }

    return result;
}

QVariantList citations(CoreDatabase &database, const QString &claimId)
{
    QVariantList result;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral("SELECT %1 FROM knowledge_citations WHERE claim_id = ? ORDER BY relative_path, start_line, id").arg(citationColumns))
                                      .bind(1, claimId)
                                      .all()) {
        result.append(nullableMap(row));
    }

    return result;
}

void setClaimStatus(CoreDatabase &database, const QString &claimId, const QString &status, const QString &reason)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_claims SET verification_status = ?, status_reason = ?, "
                     "verified_at = CASE WHEN ? = 'verified' THEN %1 ELSE verified_at END, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, status)
        .bind(2, optionalText(reason))
        .bind(3, status)
        .bind(4, claimId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("The claim no longer exists."));
    }

    // Keep confidence in step with the new status.
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT claim_type AS claimType, origin, model_confidence AS modelConfidence, "
                             "(SELECT COUNT(DISTINCT chunk_hash) FROM knowledge_citations c WHERE c.claim_id = k.id "
                             "AND c.status IN ('valid', 'moved', 'reanchored')) AS passages FROM knowledge_claims k WHERE id = ?"
                         ))
                         .bind(1, claimId)
                         .one();
    const QVariant modelValue = row->value(QStringLiteral("modelConfidence"));
    database.prepare(QStringLiteral("UPDATE knowledge_claims SET confidence = ? WHERE id = ?"))
        .bind(1, confidenceFor(status, row->value(QStringLiteral("claimType")).toString(), row->value(QStringLiteral("origin")).toString(),
                               row->value(QStringLiteral("passages")).toInt(), modelValue.isNull() ? -1 : modelValue.toDouble()))
        .bind(2, claimId)
        .run();
}

void moveClaims(CoreDatabase &database, const QString &fromPageId, const QString &toPageId)
{
    // A claim the target already makes is kept once: the target's copy
    // takes the source's citations, the duplicate is retracted.
    const auto duplicates = database
                                .prepare(QStringLiteral(
                                    "SELECT s.id AS sourceId, t.id AS targetId FROM knowledge_claims s "
                                    "INNER JOIN knowledge_claims t ON t.page_id = ?2 AND t.statement_key = s.statement_key "
                                    "WHERE s.page_id = ?1"
                                ))
                                .bind(1, fromPageId)
                                .bind(2, toPageId)
                                .all();

    for (const QVariantMap &pair : duplicates) {
        const QString sourceId = pair.value(QStringLiteral("sourceId")).toString();
        const QString targetId = pair.value(QStringLiteral("targetId")).toString();
        database
            .prepare(QStringLiteral(
                "INSERT OR IGNORE INTO knowledge_citations (id, library_id, claim_id, source_type, library_file_id, relative_path, chunk_id, "
                "chunk_hash, quote, quote_key, start_line, end_line, file_content_hash, producer_chat_id, producer_agent_id, producer_run_id, "
                "producer_job_id, status, status_detail, checked_at, created_at) "
                "SELECT lower(hex(randomblob(16))), library_id, ?2, source_type, library_file_id, relative_path, chunk_id, chunk_hash, quote, "
                "quote_key, start_line, end_line, file_content_hash, producer_chat_id, producer_agent_id, producer_run_id, producer_job_id, "
                "status, status_detail, checked_at, created_at FROM knowledge_citations WHERE claim_id = ?1"
            ))
            .bind(1, sourceId)
            .bind(2, targetId)
            .run();
        setClaimStatus(database, sourceId, Verification::retracted, QStringLiteral("Merged into the same claim on another page."));
        refreshClaimSupport(database, targetId);
    }

    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_claims SET page_id = ?2, position = position + 1000, updated_at = %1 WHERE page_id = ?1 "
                     "AND NOT EXISTS (SELECT 1 FROM knowledge_claims t WHERE t.page_id = ?2 AND t.statement_key = knowledge_claims.statement_key)"
        )
                     .arg(nowSql))
        .bind(1, fromPageId)
        .bind(2, toPageId)
        .run();
}

bool addRelation(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fromType,
    const QString &fromId,
    const QString &relationType,
    const QString &toType,
    const QString &toId,
    const QString &origin,
    const QString &explanation,
    const QString &proposalId
)
{
    if (!Relation::all().contains(relationType)) {
        throw CoreError(QStringLiteral("Unknown relationship: %1").arg(relationType));
    }

    if (fromType == toType && fromId == toId) {
        throw CoreError(QStringLiteral("A Wiki record cannot relate to itself."));
    }

    // Library isolation: both ends must exist in this Library.
    if (recordLibrary(database, fromType, fromId) != libraryId || recordLibrary(database, toType, toId) != libraryId) {
        throw CoreError(QStringLiteral("Both ends of a relationship must belong to the same Library."));
    }

    database
        .prepare(QStringLiteral(
            "INSERT INTO knowledge_relations (id, library_id, from_type, from_id, relation_type, to_type, to_id, origin, explanation, proposal_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(from_type, from_id, relation_type, to_type, to_id) DO NOTHING"
        ))
        .bind(1, newId())
        .bind(2, libraryId)
        .bind(3, fromType)
        .bind(4, fromId)
        .bind(5, relationType)
        .bind(6, toType)
        .bind(7, toId)
        .bind(8, origin)
        .bind(9, optionalText(explanation))
        .bind(10, optionalText(proposalId))
        .run();
    const bool added = database.changes() == 1;

    if (added && relationType == Relation::contradicts && fromType == QStringLiteral("claim") && toType == QStringLiteral("claim")) {
        // Neither side wins automatically: both stay visible as contested
        // until a person verifies one or retracts the other.
        for (const QString &id : {fromId, toId}) {
            const auto item = claim(database, id);

            if (item && item->value(QStringLiteral("verificationStatus")).toString() != Verification::retracted
                && item->value(QStringLiteral("verificationStatus")).toString() != Verification::verified) {
                setClaimStatus(database, id, Verification::contested, QStringLiteral("Contradicted by another claim."));
            }
        }
    }

    return added;
}

QVariantList relations(CoreDatabase &database, const QString &type, const QString &id)
{
    QVariantList result;
    const auto rows = database
                          .prepare(QStringLiteral(
                              "SELECT id, 'out' AS direction, relation_type AS relationType, to_type AS otherType, to_id AS otherId, "
                              "origin, explanation, created_at AS createdAt FROM knowledge_relations WHERE from_type = ?1 AND from_id = ?2 "
                              "UNION ALL "
                              "SELECT id, 'in' AS direction, relation_type AS relationType, from_type AS otherType, from_id AS otherId, "
                              "origin, explanation, created_at AS createdAt FROM knowledge_relations WHERE to_type = ?1 AND to_id = ?2 "
                              "ORDER BY relationType, direction, id"
                          ))
                          .bind(1, type)
                          .bind(2, id)
                          .all();

    for (const QVariantMap &row : rows) {
        QVariantMap item = nullableMap(row);
        item.insert(QStringLiteral("otherLabel"), recordLabel(database, row.value(QStringLiteral("otherType")).toString(), row.value(QStringLiteral("otherId")).toString()));
        result.append(item);
    }

    return result;
}

QVariantMap overview(CoreDatabase &database, const QString &libraryId)
{
    const auto claimsRow = database
                               .prepare(QStringLiteral(
                                   "SELECT COUNT(*) AS claims, "
                                   "SUM(verification_status = 'supported') AS supported, SUM(verification_status = 'verified') AS verified, "
                                   "SUM(verification_status = 'stale') AS stale, SUM(verification_status = 'contested') AS contested, "
                                   "SUM(verification_status = 'unverified') AS unverified "
                                   "FROM knowledge_claims k INNER JOIN knowledge_pages p ON p.id = k.page_id "
                                   "WHERE k.library_id = ? AND p.status = 'active' AND k.verification_status != 'retracted'"
                               ))
                               .bind(1, libraryId)
                               .one();
    const auto count = [&](const QString &sql) {
        return database.prepare(sql).bind(1, libraryId).scalar().toInt();
    };

    return {
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("pages"), count(QStringLiteral("SELECT COUNT(*) FROM knowledge_pages WHERE library_id = ? AND status = 'active'"))},
        {QStringLiteral("claims"), claimsRow->value(QStringLiteral("claims")).toInt()},
        {QStringLiteral("supported"), claimsRow->value(QStringLiteral("supported")).toInt()},
        {QStringLiteral("verified"), claimsRow->value(QStringLiteral("verified")).toInt()},
        {QStringLiteral("stale"), claimsRow->value(QStringLiteral("stale")).toInt()},
        {QStringLiteral("contested"), claimsRow->value(QStringLiteral("contested")).toInt()},
        {QStringLiteral("unverified"), claimsRow->value(QStringLiteral("unverified")).toInt()},
        {QStringLiteral("pendingProposals"), count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals WHERE library_id = ? AND status = 'pending'"))},
        {QStringLiteral("activeJobs"), count(QStringLiteral("SELECT COUNT(*) FROM knowledge_jobs WHERE library_id = ? AND status IN ('queued', 'running')"))},
        {QStringLiteral("lastReconciledAt"), nullable(database.prepare(QStringLiteral("SELECT MAX(reconciled_at) FROM knowledge_pages WHERE library_id = ?")).bind(1, libraryId).scalar())},
    };
}
}
