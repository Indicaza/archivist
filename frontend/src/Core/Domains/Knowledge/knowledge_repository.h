#pragma once

#include "Core/Database/core_database.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Every SQL statement over the Knowledge Store's records (migration 23):
// pages, claims, citations, relationships and revisions. Plain functions on
// one connection, like the other repositories. Writers join the caller's
// transaction or open one (Transactions::join).
//
// Results are QVariantMaps with camelCase keys, the shape QML reads.
namespace KnowledgeRepository
{
// A passage of an original Library source backing a claim.
struct CitationDraft
{
    QString fileId;
    QString relativePath;
    QString chunkId;
    QString chunkHash;       // content hash of the cited chunk when it was cited
    QString quote;           // the exact passage, as it appears in the chunk
    int startLine = 0;
    int endLine = 0;
    QString fileContentHash; // library_documents.content_hash when cited
    QString producerChatId;
    QString producerAgentId;
    QString producerRunId;
    QString producerJobId;
};

struct ClaimDraft
{
    QString type;
    QString statement;
    double modelConfidence = -1; // what the model said, if anything; never trusted on its own
    QString origin;              // Knowledge::Origin::ai or ::manual
    int position = 0;
    QList<CitationDraft> citations;
};

struct PageDraft
{
    QString libraryId;
    QString slug;   // empty: derived from the title, made unique in the Library
    QString title;
    QString pageType;
    QString content;
    QString origin;
};

struct ActorRef
{
    QString type; // Knowledge::Actor::*
    QString id;   // user name, model id, job id, ...
};

// ---- Pages ---------------------------------------------------------------

// Pages of a Library, title order. Inactive (deprecated, merged) pages only
// with `includeInactive`. Each: {id, libraryId, slug, title, pageType,
// status, reviewStatus, revision, origin, virtualPath, claimCount,
// staleClaimCount, contestedClaimCount, pendingProposalCount, updatedAt, ...}.
[[nodiscard]] QVariantList pages(CoreDatabase &database, const QString &libraryId, bool includeInactive = false);
// The page row alone (no claims).
[[nodiscard]] std::optional<QVariantMap> pageRow(CoreDatabase &database, const QString &pageId);
// The page with `content`, `claims` (each with `citations` and
// `relations`), `relations` of the page itself, and `revisions` (newest
// first, without content).
[[nodiscard]] std::optional<QVariantMap> pageDetail(CoreDatabase &database, const QString &pageId);
[[nodiscard]] std::optional<QVariantMap> pageBySlug(CoreDatabase &database, const QString &libraryId, const QString &slug);
// "Wiki/<slug>.md": where the page shows in the Library's virtual tree.
[[nodiscard]] QString virtualPath(const QString &slug);
// A slug derived from `title` that no other page of the Library uses.
[[nodiscard]] QString uniqueSlug(CoreDatabase &database, const QString &libraryId, const QString &title);

// Creates a page at revision 1. Add its claims, then call commitRevision()
// to record revision 1. Returns the page id.
QString createPage(CoreDatabase &database, const PageDraft &draft);
// Changes title/type/content without recording a revision; callers finish
// with commitRevision().
void updatePageFields(CoreDatabase &database, const QString &pageId, const QString &title, const QString &pageType, const QString &content);
void setPageStatus(CoreDatabase &database, const QString &pageId, const QString &status, const QString &mergedInto = {});
void setReviewStatus(CoreDatabase &database, const QString &pageId, const QString &reviewStatus);
void markManuallyEdited(CoreDatabase &database, const QString &pageId);
void touchReconciled(CoreDatabase &database, const QString &pageId);

// Stores an immutable snapshot of the page and its claims as a new
// revision (the first call after createPage() records revision 1 without
// bumping). Returns the revision number.
int commitRevision(CoreDatabase &database, const QString &pageId, const QString &changeKind, const ActorRef &actor, const QString &proposalId = {}, const QString &note = {});

// ---- Revisions -----------------------------------------------------------

// Newest first: {id, revision, previousRevisionId, title, pageType, status,
// changeKind, actorType, actorId, proposalId, note, createdAt}.
[[nodiscard]] QVariantList revisions(CoreDatabase &database, const QString &pageId);
// One revision with `content` and the `claims` snapshot.
[[nodiscard]] std::optional<QVariantMap> revision(CoreDatabase &database, const QString &revisionId);

// ---- Claims --------------------------------------------------------------

struct ClaimWrite
{
    QString claimId;
    bool created = false;   // a new claim, not an existing one with the same statement
    int citationsAdded = 0; // citations that were not already recorded
};

// Adds the claim to the page, or finds the page's claim with the same
// statement key and adds only the citations it does not have yet. A
// retracted claim found this way comes back. Support and confidence are
// recomputed.
ClaimWrite upsertClaim(CoreDatabase &database, const QString &libraryId, const QString &pageId, const ClaimDraft &draft);
// Adds one citation; false when the claim already cites that passage.
bool addCitation(CoreDatabase &database, const QString &libraryId, const QString &claimId, const CitationDraft &citation);
[[nodiscard]] std::optional<QVariantMap> claim(CoreDatabase &database, const QString &claimId);
// The page's claims in position order, retracted ones only on request.
[[nodiscard]] QVariantList claims(CoreDatabase &database, const QString &pageId, bool includeRetracted = false);
[[nodiscard]] QVariantList citations(CoreDatabase &database, const QString &claimId);
void setClaimStatus(CoreDatabase &database, const QString &claimId, const QString &status, const QString &reason = {});
void moveClaims(CoreDatabase &database, const QString &fromPageId, const QString &toPageId);

// Re-derives a claim's verification status and confidence from its
// citations (see KNOWLEDGE.md, "Confidence"). Returns the new status.
QString refreshClaimSupport(CoreDatabase &database, const QString &claimId);

// Deterministic confidence. Depends only on the claim's status, type,
// origin, how many distinct original passages support it and the model's
// stated confidence (which can only lower it). Repeating a claim, or
// citing the same passage again, changes nothing.
[[nodiscard]] double confidenceFor(const QString &status, const QString &claimType, const QString &origin, int supportingPassages, double modelConfidence);

// ---- Relations -----------------------------------------------------------

// Records a relationship between two records of the same Library. Throws
// when either end is missing or in another Library; returns false when it
// already exists. A `contradicts` relation marks both claims contested.
bool addRelation(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fromType,
    const QString &fromId,
    const QString &relationType,
    const QString &toType,
    const QString &toId,
    const QString &origin,
    const QString &explanation = {},
    const QString &proposalId = {}
);
// Both directions: {id, direction: "out"|"in", relationType, otherType,
// otherId, otherLabel, origin, explanation, createdAt}.
[[nodiscard]] QVariantList relations(CoreDatabase &database, const QString &type, const QString &id);

// ---- Overview --------------------------------------------------------------

// {libraryId, pages, claims, supported, verified, stale, contested,
//  unverified, pendingProposals, activeJobs, lastReconciledAt}
[[nodiscard]] QVariantMap overview(CoreDatabase &database, const QString &libraryId);
}
