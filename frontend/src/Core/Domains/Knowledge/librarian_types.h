#pragma once

#include <QString>
#include <QStringList>

// The vocabulary of the Librarian Engine (see LIBRARIAN.md). Every value
// here is also a CHECK constraint in migration 24.
namespace Librarian
{
// The three librarians. They share one worker (the Wiki job runner) and
// differ only in the jobs they queue and the policy those jobs follow.
namespace Kind
{
inline const QString maintenance = QStringLiteral("maintenance");
inline const QString research = QStringLiteral("research");
inline const QString verification = QStringLiteral("verification");

[[nodiscard]] inline QStringList all()
{
    return {maintenance, research, verification};
}

[[nodiscard]] inline bool isValid(const QString &kind)
{
    return all().contains(kind);
}
}

// What started a run.
namespace Trigger
{
inline const QString change = QStringLiteral("change");     // the Library's files changed
inline const QString idle = QStringLiteral("idle");         // Archivist was idle
inline const QString schedule = QStringLiteral("schedule"); // the scheduled interval came round
inline const QString manual = QStringLiteral("manual");     // a person pressed Start
}

// When a Library's librarians work without being asked.
namespace Mode
{
inline const QString manual = QStringLiteral("manual");       // never: only when a person starts a run
inline const QString idle = QStringLiteral("idle");           // after changes, and while Archivist is idle
inline const QString scheduled = QStringLiteral("scheduled"); // after changes, and every N minutes

[[nodiscard]] inline bool isValid(const QString &mode)
{
    return mode == manual || mode == idle || mode == scheduled;
}
}

namespace RunStatus
{
inline const QString running = QStringLiteral("running");
inline const QString completed = QStringLiteral("completed");
inline const QString failed = QStringLiteral("failed");
inline const QString cancelled = QStringLiteral("cancelled");
}

namespace FindingKind
{
inline const QString staleCitation = QStringLiteral("stale-citation");
inline const QString unsupportedClaim = QStringLiteral("unsupported-claim");
inline const QString contradiction = QStringLiteral("contradiction");
inline const QString suspiciousAssumption = QStringLiteral("suspicious-assumption");
inline const QString coverageGap = QStringLiteral("coverage-gap");
inline const QString deprecationCandidate = QStringLiteral("deprecation-candidate");
}

namespace FindingStatus
{
inline const QString open = QStringLiteral("open");          // needs a person's attention
inline const QString proposed = QStringLiteral("proposed");  // a pending proposal addresses it
inline const QString dismissed = QStringLiteral("dismissed"); // a person said no (or rejected the proposal)
inline const QString resolved = QStringLiteral("resolved");  // the proposal was applied
}

namespace TopicOrigin
{
inline const QString subsystem = QStringLiteral("subsystem"); // a folder of indexed sources
inline const QString question = QStringLiteral("question");   // an open question a page raised
inline const QString manual = QStringLiteral("manual");       // a person asked for it
}

namespace TopicStatus
{
inline const QString open = QStringLiteral("open");
inline const QString queued = QStringLiteral("queued");               // an investigation job is waiting or running
inline const QString proposed = QStringLiteral("proposed");           // a proposal waits for review
inline const QString covered = QStringLiteral("covered");             // the Wiki covers it for this evidence
inline const QString needsRevisit = QStringLiteral("needs-revisit");  // its evidence changed since
inline const QString dismissed = QStringLiteral("dismissed");         // a person rejected the proposal
inline const QString exhausted = QStringLiteral("exhausted");         // nothing citable was found
}

// Queue order: a person's own Wiki jobs first, then maintenance, then
// verification, then research.
namespace Priority
{
inline constexpr int user = 100;
inline constexpr int maintenance = 30;
inline constexpr int verification = 20;
inline constexpr int research = 10;

[[nodiscard]] inline int forKind(const QString &kind)
{
    return kind == Kind::maintenance ? maintenance : kind == Kind::verification ? verification : kind == Kind::research ? research : user;
}
}

// "librarian:research": the requested_by / actor id of a librarian.
[[nodiscard]] inline QString actorId(const QString &kind)
{
    return QStringLiteral("librarian:") + kind;
}

inline const QString defaultModel = QStringLiteral("gpt-5-mini");
}
