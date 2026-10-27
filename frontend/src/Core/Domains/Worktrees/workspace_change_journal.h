#pragma once

#include "Core/Database/core_database.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Durable, timestamped record of what changed inside a Library or worktree.
// This is the contract a future "Agent Theater" replays; it holds facts, not
// presentation.
//
// Every event says how Archivist knows about it (`evidence`):
// - performed: Archivist made the change itself and knows who asked for it
//   (an editor save by the user, an approved AI edit by an agent run);
// - observed:  Archivist saw the change on disk or in Git afterwards and does
//   not know who made it (a terminal command, another app, an agent's shell);
// - inferred:  Archivist attributes an observed change to an actor by
//   circumstance only (for example, the run that was active in the worktree).
// File watchers can't tell which process wrote a file, so observed changes are
// never reported as performed by an agent.
//
// Event kinds (`kind`):
//   file.created  file.modified  file.deleted  file.renamed  directory.created
//   git.head      (branch or HEAD commit moved)
//   checkpoint    (a point-in-time summary: HEAD, branch, dirty fingerprint)
//   run.status    (an AI run in this worktree started or settled)
//   worktree.created  worktree.opened  worktree.removed
//
// Rows are ordered by a global, gap-tolerant `sequence`; readers page with
// "events after sequence N" so playback can resume exactly where it stopped.
namespace WorkspaceChangeJournal
{
struct Event
{
    QString kind;
    QString evidence = QStringLiteral("performed");
    QString actorType = QStringLiteral("unknown"); // user, agent, external, system, unknown
    QString actorId;    // agent id for agents
    QString runId;
    QString chatId;
    QString proposalId;
    QString path;       // Library-relative, forward slashes
    QString previousPath;
    QString beforeHash;
    QString afterHash;
    QVariantMap details;
    QString occurredAt; // empty -> now
};

[[nodiscard]] bool isKnownKind(const QString &kind);

// Appends one event and returns its sequence. Throws CoreError for an unknown
// kind, evidence or actor type.
qint64 append(CoreDatabase &database, const QString &libraryId, const Event &event);
void appendAll(CoreDatabase &database, const QString &libraryId, const QList<Event> &events);

// {sequence, id, libraryId, repositoryKey, occurredAt, kind, evidence,
//  actor: {type, id}, runId, chatId, proposalId, path, previousPath,
//  beforeHash, afterHash, details}
[[nodiscard]] QVariantList eventsAfter(CoreDatabase &database, const QString &libraryId, qint64 afterSequence, int limit = 500);
[[nodiscard]] QVariantList eventsForRun(CoreDatabase &database, const QString &runId, qint64 afterSequence, int limit = 500);
[[nodiscard]] qint64 latestSequence(CoreDatabase &database, const QString &libraryId);

// A cheap picture of a worktree's working state: the changed paths Git
// reports, each with a size/mtime signature, plus HEAD and branch. Comparing
// two snapshots yields observed events without walking the whole tree.
struct Snapshot
{
    bool available = false;
    QString head;
    QString branch;
    QHash<QString, QString> entries; // Library-relative path -> signature ("" when absent)

    // Stable digest of the working state, for checkpoints.
    [[nodiscard]] QString fingerprint() const;
};

// `libraryRoot` may be a subfolder of the worktree; only paths inside it are
// kept, relative to it. Never throws.
[[nodiscard]] Snapshot capture(const QString &libraryRoot);

// Observed events that turn `before` into `after`.
[[nodiscard]] QList<Event> diff(const Snapshot &before, const Snapshot &after);
}
