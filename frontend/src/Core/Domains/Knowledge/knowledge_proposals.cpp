#include "knowledge_proposals.h"

#include "knowledge_text.h"
#include "knowledge_types.h"

#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <QHash>
#include <QSet>

using namespace CoreValues;
using namespace Knowledge;
using KnowledgeRepository::CitationDraft;
using KnowledgeRepository::ClaimDraft;
using KnowledgeRepository::ActorRef;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString proposalColumns = QStringLiteral(
    "x.id, x.library_id AS libraryId, x.job_id AS jobId, x.proposal_type AS proposalType, x.target_page_id AS targetPageId, "
    "p.title AS targetTitle, x.base_revision AS baseRevision, x.title, x.explanation, x.payload, x.evidence, x.diff, "
    "x.validation, x.fingerprint, x.status, x.actor_type AS actorType, x.actor_id AS actorId, x.reviewed_by AS reviewedBy, "
    "x.review_note AS reviewNote, x.reviewed_at AS reviewedAt, x.applied_revision AS appliedRevision, "
    "x.created_at AS createdAt, x.updated_at AS updatedAt"
);

QVariantMap proposalMap(const QVariantMap &row)
{
    QVariantMap map;

    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        map.insert(it.key(), nullable(it.value()));
    }

    map.insert(QStringLiteral("payload"), JsonValues::parseObject(row.value(QStringLiteral("payload")).toString()));
    map.insert(QStringLiteral("evidence"), JsonValues::parse(row.value(QStringLiteral("evidence")).toString()).toList());
    map.insert(QStringLiteral("validation"), JsonValues::parseObject(row.value(QStringLiteral("validation")).toString()));
    return map;
}

CitationDraft citationFrom(const QVariantMap &value)
{
    CitationDraft citation;
    citation.fileId = value.value(QStringLiteral("fileId")).toString();
    citation.relativePath = value.value(QStringLiteral("relativePath")).toString();
    citation.chunkId = value.value(QStringLiteral("chunkId")).toString();
    citation.chunkHash = value.value(QStringLiteral("chunkHash")).toString();
    citation.quote = value.value(QStringLiteral("quote")).toString();
    citation.startLine = value.value(QStringLiteral("startLine")).toInt();
    citation.endLine = value.value(QStringLiteral("endLine")).toInt();
    citation.fileContentHash = value.value(QStringLiteral("fileContentHash")).toString();
    citation.producerChatId = value.value(QStringLiteral("producerChatId")).toString();
    citation.producerAgentId = value.value(QStringLiteral("producerAgentId")).toString();
    citation.producerRunId = value.value(QStringLiteral("producerRunId")).toString();
    citation.producerJobId = value.value(QStringLiteral("producerJobId")).toString();
    return citation;
}

void finish(CoreDatabase &database, const QString &proposalId, const QString &status, const QString &reviewer, const QString &note, int revision)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_proposals SET status = ?, reviewed_by = ?, review_note = ?, reviewed_at = %1, "
                     "applied_revision = ?, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, status)
        .bind(2, reviewer.isEmpty() ? QVariant() : QVariant(reviewer))
        .bind(3, note.isEmpty() ? QVariant() : QVariant(note))
        .bind(4, revision > 0 ? QVariant(revision) : QVariant())
        .bind(5, proposalId)
        .run();
}

QVariantMap outcome(const QString &proposalId, const QString &status, const QString &pageId, int revision, const QString &message = {})
{
    return {
        {QStringLiteral("proposalId"), proposalId},
        {QStringLiteral("status"), status},
        {QStringLiteral("pageId"), pageId.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(pageId)},
        {QStringLiteral("revision"), revision > 0 ? QVariant(revision) : QVariant::fromValue(nullptr)},
        {QStringLiteral("message"), message},
    };
}

// Writes the claims and relationships of a create/modify payload.
void applyContent(CoreDatabase &database, const QVariantMap &proposal, const QString &pageId)
{
    const QString libraryId = proposal.value(QStringLiteral("libraryId")).toString();
    const QString proposalId = proposal.value(QStringLiteral("id")).toString();
    const QVariantMap payload = proposal.value(QStringLiteral("payload")).toMap();
    QHash<QString, QString> claimForRef;
    int position = 0;

    for (const QVariant &value : payload.value(QStringLiteral("claims")).toList()) {
        const QVariantMap item = value.toMap();
        ClaimDraft draft;
        draft.type = item.value(QStringLiteral("type")).toString();
        draft.statement = item.value(QStringLiteral("statement")).toString();
        draft.modelConfidence = item.contains(QStringLiteral("modelConfidence")) && !item.value(QStringLiteral("modelConfidence")).isNull()
            ? item.value(QStringLiteral("modelConfidence")).toDouble()
            : -1;
        draft.origin = Origin::ai;
        draft.position = position++;

        for (const QVariant &citation : item.value(QStringLiteral("citations")).toList()) {
            CitationDraft draftCitation = citationFrom(citation.toMap());

            if (draftCitation.producerJobId.isEmpty()) {
                draftCitation.producerJobId = proposal.value(QStringLiteral("jobId")).toString();
            }

            draft.citations.append(draftCitation);
        }

        const auto write = KnowledgeRepository::upsertClaim(database, libraryId, pageId, draft);
        claimForRef.insert(item.value(QStringLiteral("ref")).toString(), write.claimId);
    }

    // A modify proposal lists the AI claims the new version drops. Manual
    // and verified claims are a person's and stay.
    for (const QVariant &value : payload.value(QStringLiteral("retractClaimIds")).toList()) {
        const auto claim = KnowledgeRepository::claim(database, value.toString());

        if (claim && claim->value(QStringLiteral("pageId")).toString() == pageId
            && claim->value(QStringLiteral("origin")).toString() == Origin::ai
            && claim->value(QStringLiteral("verificationStatus")).toString() != Verification::verified
            && !claimForRef.values().contains(value.toString())) {
            KnowledgeRepository::setClaimStatus(database, value.toString(), Verification::retracted, QStringLiteral("Dropped by an approved revision."));
        }
    }

    const auto endpoint = [&](const QVariantMap &end) -> QPair<QString, QString> {
        if (end.contains(QStringLiteral("ref"))) {
            return {QStringLiteral("claim"), claimForRef.value(end.value(QStringLiteral("ref")).toString())};
        }

        if (end.contains(QStringLiteral("claimId"))) {
            return {QStringLiteral("claim"), end.value(QStringLiteral("claimId")).toString()};
        }

        if (end.contains(QStringLiteral("pageId"))) {
            return {QStringLiteral("page"), end.value(QStringLiteral("pageId")).toString()};
        }

        return {};
    };

    for (const QVariant &value : payload.value(QStringLiteral("relations")).toList()) {
        const QVariantMap relation = value.toMap();
        const auto from = endpoint(relation.value(QStringLiteral("from")).toMap());
        const auto to = endpoint(relation.value(QStringLiteral("to")).toMap());

        if (from.second.isEmpty() || to.second.isEmpty()) {
            continue;
        }

        KnowledgeRepository::addRelation(database, libraryId, from.first, from.second, relation.value(QStringLiteral("type")).toString(), to.first,
                                         to.second, Origin::ai, relation.value(QStringLiteral("explanation")).toString(), proposalId);
    }
}

ActorRef proposer(const QVariantMap &proposal)
{
    return {proposal.value(QStringLiteral("actorType")).toString(), proposal.value(QStringLiteral("actorId")).toString()};
}

QString approvalNote(const QString &reviewer, const QString &note)
{
    QString text = QStringLiteral("Approved by %1.").arg(reviewer.isEmpty() ? QStringLiteral("the user") : reviewer);
    return note.isEmpty() ? text : text + QLatin1Char(' ') + note;
}
}

namespace KnowledgeProposals
{
QString fingerprint(const ProposalDraft &draft)
{
    return KnowledgeText::sha256(QStringList{
        draft.proposalType,
        draft.targetPageId,
        QString::number(draft.baseRevision),
        JsonValues::toJsonString(draft.payload),
    }.join(QLatin1Char('\x1f')));
}

Submission submit(CoreDatabase &database, const ProposalDraft &draft)
{
    if (draft.title.trimmed().isEmpty()) {
        throw CoreError(QStringLiteral("A Wiki proposal needs a title."));
    }

    return Transactions::join(database, [&]() {
        Submission submission;
        const QString print = fingerprint(draft);
        const QVariant existing = database
                                      .prepare(QStringLiteral("SELECT id FROM knowledge_proposals WHERE library_id = ? AND fingerprint = ? AND status = 'pending'"))
                                      .bind(1, draft.libraryId)
                                      .bind(2, print)
                                      .scalar();

        if (existing.isValid() && !existing.isNull()) {
            submission.proposalId = existing.toString();
            return submission;
        }

        // A newer content proposal replaces older pending ones for the page;
        // a newer reconciliation replaces older reconciliations.
        if (!draft.targetPageId.isEmpty() && (draft.proposalType == ProposalType::modify || draft.proposalType == ProposalType::reconcile)) {
            database
                .prepare(QStringLiteral(
                             "UPDATE knowledge_proposals SET status = 'superseded', review_note = 'A newer proposal replaced it.', updated_at = %1 "
                             "WHERE target_page_id = ? AND status = 'pending' AND proposal_type IN (%2)"
                )
                             .arg(nowSql, draft.proposalType == ProposalType::modify ? QStringLiteral("'create', 'modify'") : QStringLiteral("'reconcile'")))
                .bind(1, draft.targetPageId)
                .run();
        }

        submission.proposalId = newId();
        submission.created = true;
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_proposals (id, library_id, job_id, proposal_type, target_page_id, base_revision, title, explanation, "
                "payload, evidence, diff, validation, fingerprint, status, actor_type, actor_id) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?, ?)"
            ))
            .bind(1, submission.proposalId)
            .bind(2, draft.libraryId)
            .bind(3, draft.jobId.isEmpty() ? QVariant() : QVariant(draft.jobId))
            .bind(4, draft.proposalType)
            .bind(5, draft.targetPageId.isEmpty() ? QVariant() : QVariant(draft.targetPageId))
            .bind(6, draft.baseRevision > 0 ? QVariant(draft.baseRevision) : QVariant())
            .bind(7, draft.title.trimmed())
            .bind(8, draft.explanation)
            .bind(9, JsonValues::toJsonString(draft.payload))
            .bind(10, JsonValues::toJsonString(draft.evidence))
            .bind(11, draft.diff)
            .bind(12, JsonValues::toJsonString(draft.validation))
            .bind(13, print)
            .bind(14, draft.actor.type.isEmpty() ? Actor::system : draft.actor.type)
            .bind(15, draft.actor.id.isEmpty() ? QVariant() : QVariant(draft.actor.id))
            .run();
        return submission;
    });
}

QVariantList proposals(CoreDatabase &database, const QString &libraryId, const QString &status)
{
    QVariantList result;
    auto statement = database.prepare(QStringLiteral(
        "SELECT %1 FROM knowledge_proposals x LEFT JOIN knowledge_pages p ON p.id = x.target_page_id "
        "WHERE x.library_id = ?1 AND (?2 = '' OR x.status = ?2) ORDER BY x.created_at DESC, x.id DESC"
    ).arg(proposalColumns));

    for (const QVariantMap &row : statement.bind(1, libraryId).bind(2, status).all()) {
        result.append(proposalMap(row));
    }

    return result;
}

std::optional<QVariantMap> proposal(CoreDatabase &database, const QString &proposalId)
{
    const auto row = database
                         .prepare(QStringLiteral("SELECT %1 FROM knowledge_proposals x LEFT JOIN knowledge_pages p ON p.id = x.target_page_id WHERE x.id = ?")
                                      .arg(proposalColumns))
                         .bind(1, proposalId)
                         .one();
    return row ? std::optional<QVariantMap>(proposalMap(*row)) : std::nullopt;
}

QVariantMap approve(CoreDatabase &database, const QString &proposalId, const QString &reviewer, const QString &note)
{
    return Transactions::join(database, [&]() -> QVariantMap {
        const auto found = proposal(database, proposalId);

        if (!found) {
            throw CoreError(QStringLiteral("The Wiki proposal no longer exists."));
        }

        const QVariantMap &item = *found;
        const QString status = item.value(QStringLiteral("status")).toString();

        if (status != ProposalStatus::pending) {
            throw CoreError(QStringLiteral("This proposal is already %1.").arg(status), {{QStringLiteral("code"), QStringLiteral("WIKI_PROPOSAL_NOT_PENDING")}});
        }

        const QString type = item.value(QStringLiteral("proposalType")).toString();
        const QString libraryId = item.value(QStringLiteral("libraryId")).toString();
        const QString targetId = item.value(QStringLiteral("targetPageId")).toString();
        const int baseRevision = item.value(QStringLiteral("baseRevision")).toInt();
        const QVariantMap payload = item.value(QStringLiteral("payload")).toMap();
        const QString text = approvalNote(reviewer, note);
        const auto conflict = [&](const QString &message) {
            finish(database, proposalId, ProposalStatus::conflicted, reviewer, message, 0);
            return outcome(proposalId, ProposalStatus::conflicted, targetId, 0, message);
        };
        const auto targetCurrent = [&]() -> std::optional<QVariantMap> {
            const auto page = KnowledgeRepository::pageRow(database, targetId);

            if (!page || page->value(QStringLiteral("status")).toString() != PageStatus::active) {
                return std::nullopt;
            }

            return page;
        };

        if (type == ProposalType::create) {
            const QVariantMap pageFields = payload.value(QStringLiteral("page")).toMap();
            KnowledgeRepository::PageDraft draft;
            draft.libraryId = libraryId;
            draft.title = pageFields.value(QStringLiteral("title")).toString();
            draft.pageType = pageFields.value(QStringLiteral("pageType")).toString();
            draft.content = pageFields.value(QStringLiteral("content")).toString();
            draft.origin = Origin::ai;
            const QString pageId = KnowledgeRepository::createPage(database, draft);
            database.prepare(QStringLiteral("UPDATE knowledge_proposals SET target_page_id = ? WHERE id = ?")).bind(1, pageId).bind(2, proposalId).run();
            applyContent(database, item, pageId);
            const int revision = KnowledgeRepository::commitRevision(database, pageId, ChangeKind::create, proposer(item), proposalId, text);
            finish(database, proposalId, ProposalStatus::applied, reviewer, note, revision);
            return outcome(proposalId, ProposalStatus::applied, pageId, revision);
        }

        if (type == ProposalType::modify) {
            const auto page = targetCurrent();

            if (!page) {
                return conflict(QStringLiteral("The page was removed, merged or deprecated after this proposal was made."));
            }

            if (page->value(QStringLiteral("revision")).toInt() != baseRevision) {
                return conflict(QStringLiteral("The page changed after this proposal was made (revision %1, proposal based on %2). Nothing was overwritten; generate a new proposal.")
                                    .arg(page->value(QStringLiteral("revision")).toInt())
                                    .arg(baseRevision));
            }

            const QVariantMap pageFields = payload.value(QStringLiteral("page")).toMap();
            KnowledgeRepository::updatePageFields(database, targetId, pageFields.value(QStringLiteral("title")).toString(),
                                                  pageFields.value(QStringLiteral("pageType")).toString(), pageFields.value(QStringLiteral("content")).toString());
            applyContent(database, item, targetId);
            KnowledgeRepository::setReviewStatus(database, targetId, ReviewStatus::reviewed);
            const int revision = KnowledgeRepository::commitRevision(database, targetId, ChangeKind::proposal, proposer(item), proposalId, text);
            finish(database, proposalId, ProposalStatus::applied, reviewer, note, revision);
            return outcome(proposalId, ProposalStatus::applied, targetId, revision);
        }

        if (type == ProposalType::deprecate) {
            const auto page = targetCurrent();

            if (!page || page->value(QStringLiteral("revision")).toInt() != baseRevision) {
                return conflict(QStringLiteral("The page changed after this proposal was made."));
            }

            KnowledgeRepository::setPageStatus(database, targetId, PageStatus::deprecated);
            const int revision = KnowledgeRepository::commitRevision(database, targetId, ChangeKind::deprecate, proposer(item), proposalId,
                                                                     text + QLatin1Char(' ') + payload.value(QStringLiteral("reason")).toString());
            finish(database, proposalId, ProposalStatus::applied, reviewer, note, revision);
            return outcome(proposalId, ProposalStatus::applied, targetId, revision);
        }

        if (type == ProposalType::merge) {
            const QString sourceId = payload.value(QStringLiteral("sourcePageId")).toString();
            const auto target = targetCurrent();
            const auto source = KnowledgeRepository::pageRow(database, sourceId);

            if (!target || target->value(QStringLiteral("revision")).toInt() != baseRevision || !source
                || source->value(QStringLiteral("status")).toString() != PageStatus::active
                || source->value(QStringLiteral("revision")).toInt() != payload.value(QStringLiteral("sourceRevision")).toInt()) {
                return conflict(QStringLiteral("One of the pages changed after this merge was proposed."));
            }

            KnowledgeRepository::moveClaims(database, sourceId, targetId);
            KnowledgeRepository::setPageStatus(database, sourceId, PageStatus::merged, targetId);
            KnowledgeRepository::addRelation(database, libraryId, QStringLiteral("page"), targetId, Relation::supersedes, QStringLiteral("page"), sourceId,
                                             Origin::system, QStringLiteral("Merged"), proposalId);
            KnowledgeRepository::commitRevision(database, sourceId, ChangeKind::merge, proposer(item), proposalId, text);
            const int revision = KnowledgeRepository::commitRevision(database, targetId, ChangeKind::merge, proposer(item), proposalId, text);
            finish(database, proposalId, ProposalStatus::applied, reviewer, note, revision);
            return outcome(proposalId, ProposalStatus::applied, targetId, revision);
        }

        if (type == ProposalType::reconcile) {
            const auto page = targetCurrent();

            if (!page) {
                return conflict(QStringLiteral("The page was removed, merged or deprecated after this proposal was made."));
            }

            // Retract only what is still unsupported now; anything that
            // recovered since (a file came back) stays.
            int retracted = 0;

            for (const QVariant &value : payload.value(QStringLiteral("retractClaimIds")).toList()) {
                const QString claimId = value.toString();

                if (KnowledgeRepository::refreshClaimSupport(database, claimId) == Verification::stale) {
                    KnowledgeRepository::setClaimStatus(database, claimId, Verification::retracted, QStringLiteral("Its sources no longer support it (approved reconciliation)."));
                    ++retracted;
                }
            }

            KnowledgeRepository::setReviewStatus(database, targetId, ReviewStatus::reviewed);
            const int revision = KnowledgeRepository::commitRevision(database, targetId, ChangeKind::reconcile, proposer(item), proposalId,
                                                                     text + QStringLiteral(" %1 unsupported claim(s) retracted.").arg(retracted));
            finish(database, proposalId, ProposalStatus::applied, reviewer, note, revision);
            return outcome(proposalId, ProposalStatus::applied, targetId, revision);
        }

        throw CoreError(QStringLiteral("Unknown proposal type: %1").arg(type));
    });
}

QVariantMap reject(CoreDatabase &database, const QString &proposalId, const QString &reviewer, const QString &note)
{
    return Transactions::join(database, [&]() {
        const auto found = proposal(database, proposalId);

        if (!found) {
            throw CoreError(QStringLiteral("The Wiki proposal no longer exists."));
        }

        if (found->value(QStringLiteral("status")).toString() != ProposalStatus::pending) {
            throw CoreError(QStringLiteral("This proposal is already %1.").arg(found->value(QStringLiteral("status")).toString()),
                            {{QStringLiteral("code"), QStringLiteral("WIKI_PROPOSAL_NOT_PENDING")}});
        }

        const QString targetId = found->value(QStringLiteral("targetPageId")).toString();

        // A rejected reconciliation means "I have seen it": stale claims stay
        // stale and visible, the page is no longer flagged.
        if (found->value(QStringLiteral("proposalType")).toString() == ProposalType::reconcile && !targetId.isEmpty()) {
            KnowledgeRepository::setReviewStatus(database, targetId, ReviewStatus::reviewed);
        }

        finish(database, proposalId, ProposalStatus::rejected, reviewer, note, 0);
        return outcome(proposalId, ProposalStatus::rejected, targetId, 0);
    });
}

Submission proposeDeprecation(CoreDatabase &database, const QString &pageId, const QString &reason, const ActorRef &actor)
{
    const auto page = KnowledgeRepository::pageRow(database, pageId);

    if (!page || page->value(QStringLiteral("status")).toString() != PageStatus::active) {
        throw CoreError(QStringLiteral("Only an active Wiki page can be deprecated."));
    }

    ProposalDraft draft;
    draft.libraryId = page->value(QStringLiteral("libraryId")).toString();
    draft.proposalType = ProposalType::deprecate;
    draft.targetPageId = pageId;
    draft.baseRevision = page->value(QStringLiteral("revision")).toInt();
    draft.title = QStringLiteral("Deprecate “%1”").arg(page->value(QStringLiteral("title")).toString());
    draft.explanation = reason;
    draft.payload = {{QStringLiteral("reason"), reason}};
    draft.actor = actor;
    return submit(database, draft);
}

Submission proposeMerge(CoreDatabase &database, const QString &sourcePageId, const QString &targetPageId, const QString &reason, const ActorRef &actor)
{
    const auto source = KnowledgeRepository::pageRow(database, sourcePageId);
    const auto target = KnowledgeRepository::pageRow(database, targetPageId);

    if (!source || !target || sourcePageId == targetPageId) {
        throw CoreError(QStringLiteral("A merge needs two different Wiki pages."));
    }

    if (source->value(QStringLiteral("libraryId")) != target->value(QStringLiteral("libraryId"))) {
        throw CoreError(QStringLiteral("Wiki pages can only be merged within one Library."));
    }

    ProposalDraft draft;
    draft.libraryId = target->value(QStringLiteral("libraryId")).toString();
    draft.proposalType = ProposalType::merge;
    draft.targetPageId = targetPageId;
    draft.baseRevision = target->value(QStringLiteral("revision")).toInt();
    draft.title = QStringLiteral("Merge “%1” into “%2”").arg(source->value(QStringLiteral("title")).toString(), target->value(QStringLiteral("title")).toString());
    draft.explanation = reason;
    draft.payload = {
        {QStringLiteral("sourcePageId"), sourcePageId},
        {QStringLiteral("sourceRevision"), source->value(QStringLiteral("revision")).toInt()},
        {QStringLiteral("targetPageId"), targetPageId},
    };
    draft.actor = actor;
    return submit(database, draft);
}

QString createManualPage(CoreDatabase &database, const QString &libraryId, const QString &title, const QString &pageType, const QString &content, const QString &user)
{
    return Transactions::join(database, [&]() {
        KnowledgeRepository::PageDraft draft;
        draft.libraryId = libraryId;
        draft.title = title;
        draft.pageType = pageType.isEmpty() ? PageType::topic : pageType;
        draft.content = content;
        draft.origin = Origin::manual;
        const QString pageId = KnowledgeRepository::createPage(database, draft);
        KnowledgeRepository::markManuallyEdited(database, pageId);
        KnowledgeRepository::commitRevision(database, pageId, ChangeKind::create, {Actor::user, user});
        return pageId;
    });
}

int editPage(CoreDatabase &database, const QString &pageId, const QString &title, const QString &pageType, const QString &content, int baseRevision, const QString &user)
{
    return Transactions::join(database, [&]() {
        const auto page = KnowledgeRepository::pageRow(database, pageId);

        if (!page) {
            throw CoreError(QStringLiteral("The Wiki page no longer exists."));
        }

        const int current = page->value(QStringLiteral("revision")).toInt();

        if (current != baseRevision) {
            throw CoreError(QStringLiteral("The page changed while you were editing it (now revision %1).").arg(current),
                            {{QStringLiteral("code"), QStringLiteral("WIKI_REVISION_CONFLICT")}, {QStringLiteral("currentRevision"), current}});
        }

        KnowledgeRepository::updatePageFields(database, pageId, title, pageType.isEmpty() ? page->value(QStringLiteral("pageType")).toString() : pageType, content);
        KnowledgeRepository::markManuallyEdited(database, pageId);
        return KnowledgeRepository::commitRevision(database, pageId, ChangeKind::manualEdit, {Actor::user, user});
    });
}

QString addManualClaim(CoreDatabase &database, const QString &pageId, const QString &type, const QString &statement, const QList<CitationDraft> &citations, const QString &user)
{
    return Transactions::join(database, [&]() {
        const auto page = KnowledgeRepository::pageRow(database, pageId);

        if (!page) {
            throw CoreError(QStringLiteral("The Wiki page no longer exists."));
        }

        ClaimDraft draft;
        draft.type = type;
        draft.statement = statement;
        draft.origin = Origin::manual;
        draft.position = 10000;
        draft.citations = citations;
        const auto write = KnowledgeRepository::upsertClaim(database, page->value(QStringLiteral("libraryId")).toString(), pageId, draft);
        KnowledgeRepository::markManuallyEdited(database, pageId);
        KnowledgeRepository::commitRevision(database, pageId, ChangeKind::manualEdit, {Actor::user, user}, {}, QStringLiteral("Added a claim."));
        return write.claimId;
    });
}

int reviewClaim(CoreDatabase &database, const QString &claimId, const QString &action, const QString &user, const QString &note)
{
    return Transactions::join(database, [&]() {
        const auto claim = KnowledgeRepository::claim(database, claimId);

        if (!claim) {
            throw CoreError(QStringLiteral("The claim no longer exists."));
        }

        const QString pageId = claim->value(QStringLiteral("pageId")).toString();
        QString summary;

        if (action == QStringLiteral("verify")) {
            KnowledgeRepository::setClaimStatus(database, claimId, Verification::verified, note);
            summary = QStringLiteral("Verified a claim.");
        } else if (action == QStringLiteral("retract")) {
            KnowledgeRepository::setClaimStatus(database, claimId, Verification::retracted, note.isEmpty() ? QStringLiteral("Retracted by %1.").arg(user) : note);
            summary = QStringLiteral("Retracted a claim.");
        } else if (action == QStringLiteral("reopen")) {
            KnowledgeRepository::setClaimStatus(database, claimId, Verification::unverified);
            KnowledgeRepository::refreshClaimSupport(database, claimId);
            summary = QStringLiteral("Reopened a claim.");
        } else {
            throw CoreError(QStringLiteral("Unknown claim review action: %1").arg(action));
        }

        return KnowledgeRepository::commitRevision(database, pageId, ChangeKind::claimReview, {Actor::user, user}, {}, note.isEmpty() ? summary : summary + QLatin1Char(' ') + note);
    });
}

int restoreRevision(CoreDatabase &database, const QString &pageId, const QString &revisionId, const QString &user)
{
    return Transactions::join(database, [&]() {
        const auto old = KnowledgeRepository::revision(database, revisionId);

        if (!old || old->value(QStringLiteral("pageId")).toString() != pageId) {
            throw CoreError(QStringLiteral("That revision does not belong to this page."));
        }

        KnowledgeRepository::updatePageFields(database, pageId, old->value(QStringLiteral("title")).toString(), old->value(QStringLiteral("pageType")).toString(),
                                              old->value(QStringLiteral("content")).toString());
        KnowledgeRepository::setPageStatus(database, pageId, old->value(QStringLiteral("status")).toString());
        QHash<QString, QString> snapshotStatus;

        for (const QVariant &value : old->value(QStringLiteral("claims")).toList()) {
            const QVariantMap item = value.toMap();
            snapshotStatus.insert(item.value(QStringLiteral("id")).toString(), item.value(QStringLiteral("verificationStatus")).toString());
        }

        for (const QVariant &value : KnowledgeRepository::claims(database, pageId, true)) {
            const QVariantMap item = value.toMap();
            const QString claimId = item.value(QStringLiteral("id")).toString();
            const QString current = item.value(QStringLiteral("verificationStatus")).toString();

            if (!snapshotStatus.contains(claimId)) {
                if (current != Verification::retracted) {
                    KnowledgeRepository::setClaimStatus(database, claimId, Verification::retracted, QStringLiteral("Not part of the restored revision."));
                }

                continue;
            }

            const QString then = snapshotStatus.value(claimId);

            if (then == Verification::retracted || then == Verification::verified) {
                KnowledgeRepository::setClaimStatus(database, claimId, then);
            } else if (current == Verification::retracted) {
                KnowledgeRepository::setClaimStatus(database, claimId, Verification::unverified);
            }

            // Sources may have changed since; support is re-checked, not copied.
            KnowledgeRepository::refreshClaimSupport(database, claimId);
        }

        return KnowledgeRepository::commitRevision(database, pageId, ChangeKind::restore, {Actor::user, user}, {},
                                                   QStringLiteral("Restored revision %1.").arg(old->value(QStringLiteral("revision")).toInt()));
    });
}
}
