#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Git's own worktree mechanism (`git worktree`), wrapped for Archivist.
//
// Everything here shells out to the user's git with an explicit `-C <path>`;
// nothing changes the process working directory. Calls block, so callers run
// them through CoreRuntime::run() or another background thread.
//
// Safety rules every mutation follows:
// - worktrees are never removed with --force, and branches are never deleted;
// - removal is refused while the worktree has uncommitted or untracked work,
//   is locked, or is the repository's main worktree;
// - a missing worktree folder is only ever pruned, which drops git's stale
//   bookkeeping and touches no files.
namespace GitWorktrees
{
struct CommandResult
{
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QString output;
    QString errors;

    [[nodiscard]] bool ok() const
    {
        return started && !timedOut && exitCode == 0;
    }
};

// Runs `git -C <directory> <arguments>` without a terminal prompt and without
// taking optional locks, so status checks never contend with a user's or an
// agent's own git commands in the same worktree.
CommandResult run(const QString &directory, const QStringList &arguments, int timeoutMilliseconds = 30000);

struct Repository
{
    bool repository = false;
    bool bare = false;
    // Absolute top level of the worktree that contains the inspected path.
    QString topLevel;
    // The shared .git directory; identical for every worktree of a repository.
    QString commonDirectory;
    // This worktree's own git directory (.git, or .git/worktrees/<name>).
    QString gitDirectory;
    // The inspected path relative to topLevel ("" at the top), no trailing slash.
    QString prefix;
    // Stable identity of the repository: the canonical common directory.
    QString key;
    // True when topLevel is a linked worktree rather than the main checkout.
    bool linkedWorktree = false;
};

// Never throws. `repository` is false outside a git work tree.
[[nodiscard]] Repository inspect(const QString &path);

struct Worktree
{
    QString path;
    QString head;
    QString branch; // short name; empty when detached or unborn
    bool detached = false;
    bool bare = false;
    bool main = false;
    bool locked = false;
    QString lockReason;
    bool prunable = false;
    QString prunableReason;
    bool exists = true;

    [[nodiscard]] QVariantMap toMap() const;
};

// `git worktree list --porcelain`. The first entry is the main worktree.
// Throws CoreError when git cannot list the repository's worktrees.
[[nodiscard]] QList<Worktree> list(const QString &repositoryPath);

// Parses porcelain output; exposed for tests. `nulSeparated` selects -z output.
[[nodiscard]] QList<Worktree> parseList(const QString &output, bool nulSeparated);

struct Status
{
    bool available = false; // false when the folder is missing or git failed
    bool missing = false;
    QString error;
    QString branch;
    QString head;
    bool detached = false;
    bool unborn = false;
    QString upstream;
    int ahead = 0;
    int behind = 0;
    int staged = 0;
    int modified = 0;
    int deleted = 0;
    int renamed = 0;
    int untracked = 0;
    int conflicted = 0;
    QStringList conflictedPaths;
    struct Change
    {
        QString path; // relative to the worktree top level; untracked folders end in '/'
        // '?' untracked, 'A' added, 'M' modified, 'D' deleted, 'R' renamed,
        // 'U' conflicted, 'S' staged-only change
        QChar code;
    };

    // Every changed path: tracked changes, untracked files and conflicts.
    QList<Change> changes;
    bool inProgressOperation = false;
    QString operation; // merge, rebase, cherry-pick, revert, bisect

    [[nodiscard]] bool dirty() const
    {
        return staged + modified + deleted + renamed + untracked + conflicted > 0;
    }

    [[nodiscard]] QVariantMap toMap() const;
};

// One cheap `git status --porcelain=v2 --branch` (untracked files, no ignored
// files). Never throws.
[[nodiscard]] Status status(const QString &worktreePath);
// Limited to `pathspec` (relative to the top level). `allUntracked` lists each
// untracked file instead of collapsing untracked folders.
[[nodiscard]] Status status(const QString &worktreePath, const QStringList &pathspec, bool allUntracked);

// Local branch short names, sorted.
[[nodiscard]] QStringList localBranches(const QString &repositoryPath);
[[nodiscard]] bool branchExists(const QString &repositoryPath, const QString &branch);
[[nodiscard]] bool isValidBranchName(const QString &repositoryPath, const QString &branch);
[[nodiscard]] bool refExists(const QString &repositoryPath, const QString &ref);

// Folder-safe slug of a branch name ("feature/login" -> "feature-login").
[[nodiscard]] QString slugForBranch(const QString &branch);

// <parent of the main worktree>/<main worktree name>.worktrees/<branch slug>,
// with a numeric suffix when that folder already exists. Never inside a
// worktree, so a Library scan of one checkout can't pick up another.
[[nodiscard]] QString suggestedPath(const QString &mainWorktreePath, const QString &branch);

struct CreateOptions
{
    QString path;      // empty -> suggestedPath()
    QString branch;    // required
    bool newBranch = true;
    QString baseRef;   // new branches only; empty -> HEAD of repositoryPath
};

// `git worktree add`. Validates first and throws CoreError with
// details.code: INVALID_BRANCH, BRANCH_EXISTS, BRANCH_NOT_FOUND,
// BRANCH_IN_USE, BASE_NOT_FOUND, PATH_EXISTS, PATH_INSIDE_WORKTREE or
// GIT_FAILED. Returns the new worktree.
Worktree create(const QString &repositoryPath, const CreateOptions &options);

struct RemovalCheck
{
    bool allowed = false;
    // {code, message} for each reason removal is refused.
    QVariantList blockers;
    Status status;
};

// What would stop a safe removal of `worktreePath`. Never throws.
[[nodiscard]] RemovalCheck removalCheck(const QString &repositoryPath, const QString &worktreePath);

// Safe `git worktree remove` (never --force). Throws CoreError with
// details.code REMOVAL_BLOCKED and details.blockers when refused.
void remove(const QString &repositoryPath, const QString &worktreePath);

// `git worktree prune`: forgets worktrees whose folders are gone.
void prune(const QString &repositoryPath);

// True for a folder whose `.git` is a file pointing into another repository's
// `worktrees/` directory, i.e. the top of a linked worktree.
[[nodiscard]] bool isLinkedWorktreeRoot(const QString &directoryPath);

// Canonical path when it exists, otherwise the cleaned absolute path.
[[nodiscard]] QString canonicalPath(const QString &path);
[[nodiscard]] bool samePath(const QString &left, const QString &right);
// True when `path` is `root` or inside it.
[[nodiscard]] bool isInside(const QString &path, const QString &root);
}
