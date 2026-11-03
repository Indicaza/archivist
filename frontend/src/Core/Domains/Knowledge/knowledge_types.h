#pragma once

#include <QString>
#include <QStringList>

// The vocabulary of the Knowledge Store (see KNOWLEDGE.md). Every value here
// is also a CHECK constraint in migration 23, so these are the only strings
// that can reach the database.
namespace Knowledge
{
namespace PageType
{
inline const QString overview = QStringLiteral("overview");
inline const QString topic = QStringLiteral("topic");
inline const QString conceptPage = QStringLiteral("concept");
inline const QString entity = QStringLiteral("entity");
inline const QString decisionLog = QStringLiteral("decision-log");
inline const QString fileSummary = QStringLiteral("file-summary");
inline const QString question = QStringLiteral("question");

[[nodiscard]] inline QStringList all()
{
    return {overview, topic, conceptPage, entity, decisionLog, fileSummary, question};
}
}

namespace PageStatus
{
inline const QString active = QStringLiteral("active");
inline const QString deprecated = QStringLiteral("deprecated");
inline const QString merged = QStringLiteral("merged");
}

namespace ReviewStatus
{
inline const QString reviewed = QStringLiteral("reviewed");
inline const QString needsReview = QStringLiteral("needs-review");
}

namespace Origin
{
inline const QString ai = QStringLiteral("ai");
inline const QString manual = QStringLiteral("manual");
inline const QString mixed = QStringLiteral("mixed");
inline const QString system = QStringLiteral("system");
}

namespace ClaimType
{
inline const QString fact = QStringLiteral("fact");
inline const QString decision = QStringLiteral("decision");
inline const QString inference = QStringLiteral("inference");
inline const QString question = QStringLiteral("question");
inline const QString assumption = QStringLiteral("assumption");

[[nodiscard]] inline QStringList all()
{
    return {fact, decision, inference, question, assumption};
}

// Facts, decisions and inferences assert something about the Library and
// must cite it. Questions and unresolved assumptions may stand alone.
[[nodiscard]] inline bool requiresEvidence(const QString &type)
{
    return type == fact || type == decision || type == inference;
}
}

// How far a claim is backed by original sources. Only `verified` involves a
// person; `supported` means Archivist checked that a cited passage exists and
// contains the quoted text, not that the claim is true.
namespace Verification
{
inline const QString unverified = QStringLiteral("unverified");
inline const QString supported = QStringLiteral("supported");
inline const QString verified = QStringLiteral("verified");
inline const QString contested = QStringLiteral("contested");
inline const QString stale = QStringLiteral("stale");
inline const QString retracted = QStringLiteral("retracted");
}

namespace CitationStatus
{
inline const QString valid = QStringLiteral("valid");             // the cited chunk is unchanged
inline const QString moved = QStringLiteral("moved");             // same text, another path or file id
inline const QString reanchored = QStringLiteral("reanchored");   // same text, new chunk id (re-indexed)
inline const QString modified = QStringLiteral("modified");       // the quoted text is no longer in the file
inline const QString deleted = QStringLiteral("deleted");         // the file (and the text) is gone
inline const QString unavailable = QStringLiteral("unavailable"); // the file exists but is not indexed now

// Statuses that still count as original-source support.
[[nodiscard]] inline bool supports(const QString &status)
{
    return status == valid || status == moved || status == reanchored;
}
}

namespace Relation
{
inline const QString dependsOn = QStringLiteral("depends-on");
inline const QString relatedTo = QStringLiteral("related-to");
inline const QString contradicts = QStringLiteral("contradicts");
inline const QString supersedes = QStringLiteral("supersedes");
inline const QString derivedFrom = QStringLiteral("derived-from");
inline const QString partOf = QStringLiteral("part-of");

[[nodiscard]] inline QStringList all()
{
    return {dependsOn, relatedTo, contradicts, supersedes, derivedFrom, partOf};
}
}

namespace ProposalType
{
inline const QString create = QStringLiteral("create");
inline const QString modify = QStringLiteral("modify");
inline const QString merge = QStringLiteral("merge");
inline const QString deprecate = QStringLiteral("deprecate");
inline const QString reconcile = QStringLiteral("reconcile");
}

namespace ProposalStatus
{
inline const QString pending = QStringLiteral("pending");
inline const QString applied = QStringLiteral("applied");
inline const QString rejected = QStringLiteral("rejected");
inline const QString superseded = QStringLiteral("superseded");
inline const QString conflicted = QStringLiteral("conflicted");
}

namespace ChangeKind
{
inline const QString create = QStringLiteral("create");
inline const QString proposal = QStringLiteral("proposal");
inline const QString manualEdit = QStringLiteral("manual-edit");
inline const QString restore = QStringLiteral("restore");
inline const QString reconcile = QStringLiteral("reconcile");
inline const QString merge = QStringLiteral("merge");
inline const QString deprecate = QStringLiteral("deprecate");
inline const QString claimReview = QStringLiteral("claim-review");
}

namespace Actor
{
inline const QString user = QStringLiteral("user");
inline const QString ai = QStringLiteral("ai");
inline const QString system = QStringLiteral("system");
}

// What a Wiki job does. Only the first three run today; the rest are the
// queue's contract with the future autonomous librarians (see KNOWLEDGE.md).
namespace JobType
{
inline const QString generatePage = QStringLiteral("generate-page");
inline const QString expandPage = QStringLiteral("expand-page");
inline const QString reconcileSources = QStringLiteral("reconcile-sources");
inline const QString investigateGap = QStringLiteral("investigate-gap");
inline const QString discoverRelated = QStringLiteral("discover-related");
inline const QString identifyContradictions = QStringLiteral("identify-contradictions");
inline const QString suggestImprovements = QStringLiteral("suggest-improvements");
inline const QString processCorrection = QStringLiteral("process-correction");
}

namespace JobStatus
{
inline const QString queued = QStringLiteral("queued");
inline const QString running = QStringLiteral("running");
inline const QString completed = QStringLiteral("completed");
inline const QString failed = QStringLiteral("failed");
inline const QString cancelled = QStringLiteral("cancelled");
}

// Embedding source types for Wiki records (shared vector tables, migration 22).
namespace EmbeddingSource
{
inline const QString page = QStringLiteral("wiki-page");
inline const QString claim = QStringLiteral("wiki-claim");
}
}
