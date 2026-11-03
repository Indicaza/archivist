#pragma once

#include "knowledge_repository.h"

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Reviewable change to the Wiki, and every path that changes a page.
//
// Nothing an AI produces reaches a page directly: generation writes a
// pending proposal, and only approve() applies it, in one transaction, with
// a check that the page has not changed since the proposal was made. People
// edit pages directly (editPage, reviewClaim, restoreRevision); every change
// of either kind records an immutable revision.
namespace KnowledgeProposals
{
using KnowledgeRepository::ActorRef;

struct ProposalDraft
{
    QString libraryId;
    QString jobId;
    QString proposalType;   // Knowledge::ProposalType::*
    QString targetPageId;   // modify, merge (target), deprecate, reconcile
    int baseRevision = 0;   // the target page's revision the proposal was made against
    QString title;
    QString explanation;
    // create/modify: {page: {title, pageType, content}, claims: [{ref, type,
    //   statement, modelConfidence, citations: [...]}], relations: [{from,
    //   type, to, explanation}], retractClaimIds: [...]}
    // merge: {sourcePageId, targetPageId}; deprecate: {reason}
    // reconcile: {staleClaimIds, citationChanges, retractClaimIds}
    QVariantMap payload;
    QVariantList evidence;  // the supporting passages, for the reviewer
    QVariantMap validation; // warnings and what was dropped, for the reviewer
    QString diff;
    ActorRef actor;
};

struct Submission
{
    QString proposalId;
    bool created = false; // false: an identical proposal is already pending
};

// Stores a pending proposal. An identical pending proposal (same
// fingerprint) is returned instead of a duplicate. Older pending
// create/modify proposals for the same page are superseded by a newer
// modify proposal.
Submission submit(CoreDatabase &database, const ProposalDraft &draft);

// The fingerprint that identifies "the same proposal": type, target, base
// revision and the payload.
[[nodiscard]] QString fingerprint(const ProposalDraft &draft);

// {id, libraryId, jobId, proposalType, targetPageId, targetTitle,
//  baseRevision, title, explanation, payload, evidence, validation, diff,
//  status, actorType, actorId, reviewedBy, reviewNote, reviewedAt,
//  appliedRevision, createdAt}. `status` empty lists every status.
[[nodiscard]] QVariantList proposals(CoreDatabase &database, const QString &libraryId, const QString &status = {});
[[nodiscard]] std::optional<QVariantMap> proposal(CoreDatabase &database, const QString &proposalId);

// Applies a pending proposal in one transaction. When the page changed
// since the proposal was made (another proposal, a manual edit, a restore)
// nothing is applied and the proposal becomes `conflicted`. Returns
// {proposalId, status, pageId, revision}.
QVariantMap approve(CoreDatabase &database, const QString &proposalId, const QString &reviewer, const QString &note = {});
QVariantMap reject(CoreDatabase &database, const QString &proposalId, const QString &reviewer, const QString &note = {});

// Proposals people (or future librarians) make without a model.
Submission proposeDeprecation(CoreDatabase &database, const QString &pageId, const QString &reason, const ActorRef &actor);
Submission proposeMerge(CoreDatabase &database, const QString &sourcePageId, const QString &targetPageId, const QString &reason, const ActorRef &actor);

// ---- Direct edits by a person --------------------------------------------

// A page a person writes. Returns the page id.
QString createManualPage(CoreDatabase &database, const QString &libraryId, const QString &title, const QString &pageType, const QString &content, const QString &user);
// Throws with details {code: "WIKI_REVISION_CONFLICT", currentRevision}
// when `baseRevision` is not the page's revision. Returns the new revision.
int editPage(CoreDatabase &database, const QString &pageId, const QString &title, const QString &pageType, const QString &content, int baseRevision, const QString &user);
// A claim a person adds; citations are validated like AI citations would be
// (see KnowledgeCitations::resolve) by the caller.
QString addManualClaim(CoreDatabase &database, const QString &pageId, const QString &type, const QString &statement, const QList<KnowledgeRepository::CitationDraft> &citations, const QString &user);
// "verify", "retract" or "reopen". Returns the new revision.
int reviewClaim(CoreDatabase &database, const QString &claimId, const QString &action, const QString &user, const QString &note = {});
// Puts a page back to an earlier revision's title, type, content and claim
// set as a new revision. Claims the old revision did not have are
// retracted, never deleted; claims it had come back and their support is
// re-checked. Returns the new revision.
int restoreRevision(CoreDatabase &database, const QString &pageId, const QString &revisionId, const QString &user);
}
