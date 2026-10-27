#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Git worktrees as Archivist workspaces.
//
// A worktree Archivist works in is a Library of its own: its own folder, file
// catalog, text index, Explorer state, terminals, language servers, Chats and
// AI runs, all keyed by that Library's id. library_worktrees links each such
// Library to the "anchor" Library it was opened from, so Collections, the
// worktree selector and the change journal know they share a repository.
//
// The anchor is the Library the user added (usually the main checkout). A
// linked worktree's Library root mirrors the anchor's position inside its
// checkout, so a Library rooted at <repo>/docs maps to <worktree>/docs.
//
// Git is the source of truth for which worktrees exist; the database only
// remembers which of them Archivist has opened. Nothing here is cached: every
// overview asks git again. Callers run these through CoreRuntime::run().
namespace WorktreeRepository
{
// The anchor of a linked worktree Library, or `libraryId` itself.
[[nodiscard]] QString anchorLibraryId(CoreDatabase &database, const QString &libraryId);

// {libraryId, anchorLibraryId, repositoryKey, worktreePath, libraryPrefix,
//  origin, createdBranch, baseRef, createdAt, updatedAt}
[[nodiscard]] std::optional<QVariantMap> linkFor(CoreDatabase &database, const QString &libraryId);

// The worktrees of the repository behind `libraryId` (any Library of it).
// {libraryId, anchorLibraryId, repository: null | {key, name, mainPath,
//  prefix}, worktrees: [...], error: null | string, refreshedAt}
// Each worktree: GitWorktrees::Worktree fields plus {id, libraryId,
// libraryName, libraryRoot, libraryArchived, anchor, linked, origin, orphaned,
// runningRuns: [{runId, chatId, agentId, agentName, phase, startedAt}]}.
// Status is not included; see statuses().
[[nodiscard]] QVariantMap overview(CoreDatabase &database, const QString &libraryId);

// Git status for each worktree path: {path: GitWorktrees::Status map}.
[[nodiscard]] QVariantMap statuses(const QStringList &worktreePaths);

// Opens (or re-opens) a worktree of the repository as a Library linked to
// the anchor. Returns {library, created, worktree}. The anchor's own checkout
// returns the anchor Library.
QVariantMap openWorktree(CoreDatabase &database, const QString &libraryId, const QString &worktreePath);

// input: {branch, newBranch (default true), baseRef, path}. Runs
// `git worktree add`, then opens the result. Returns openWorktree()'s result.
QVariantMap createWorktree(CoreDatabase &database, const QString &libraryId, const QVariantMap &input);

// {allowed, blockers: [{code, message}], status, libraryId}. Adds Archivist's
// own blockers to git's: the anchor's checkout, running AI runs in the
// worktree's Library, and `unsavedDocuments` open editors with unsaved edits.
[[nodiscard]] QVariantMap removalPreview(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &worktreePath,
    int unsavedDocuments
);

// Safe removal: re-checks removalPreview(), runs `git worktree remove` without
// --force (or prunes a folder that is already gone), keeps the branch, and
// archives the worktree's Library so its Chats and history survive. Throws
// CoreError {code: REMOVAL_BLOCKED, blockers} when refused.
// Returns {removed: true, path, archivedLibraryId}.
QVariantMap removeWorktree(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &worktreePath,
    int unsavedDocuments
);

// Archives the Library of a worktree git no longer knows about and drops the
// link. Refuses while git still lists the worktree.
QVariantMap forgetWorktree(CoreDatabase &database, const QString &worktreeLibraryId);

// The running AI runs per Library id: {libraryId: [{runId, chatId, agentId,
// agentName, phase, startedAt}]}.
[[nodiscard]] QVariantMap runningRunsByLibrary(CoreDatabase &database, const QStringList &libraryIds);

// Where an AI run executes, recorded when it starts:
// {workspaceRoot, repositoryKey, branch, head} (empty strings when unknown).
[[nodiscard]] QVariantMap executionContext(const QString &libraryRoot);
}
