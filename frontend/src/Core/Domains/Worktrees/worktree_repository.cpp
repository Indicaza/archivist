#include "worktree_repository.h"

#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Worktrees/git_worktrees.h"
#include "Core/Domains/Worktrees/workspace_change_journal.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

[[noreturn]] void fail(const QString &code, const QString &message, QVariantMap details = {})
{
    details.insert(QStringLiteral("code"), code);
    throw CoreError(message, details);
}

QVariantMap requireLibrary(CoreDatabase &database, const QString &libraryId)
{
    if (!isUuid(libraryId)) {
        throw CoreError(QStringLiteral("Invalid Library ID."));
    }

    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    return *library;
}

QVariantMap mapLink(const QVariantMap &row)
{
    return {
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("anchorLibraryId"), row.value(QStringLiteral("anchor_library_id"))},
        {QStringLiteral("repositoryKey"), row.value(QStringLiteral("repository_key"))},
        {QStringLiteral("worktreePath"), row.value(QStringLiteral("worktree_path"))},
        {QStringLiteral("libraryPrefix"), row.value(QStringLiteral("library_prefix"))},
        {QStringLiteral("origin"), row.value(QStringLiteral("origin"))},
        {QStringLiteral("createdBranch"), nullable(row.value(QStringLiteral("created_branch")))},
        {QStringLiteral("baseRef"), nullable(row.value(QStringLiteral("base_ref")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

const QString linkColumns = QStringLiteral(
    "library_id, anchor_library_id, repository_key, worktree_path, library_prefix, origin, created_branch, "
    "base_ref, created_at, updated_at"
);

QString joinPrefix(const QString &worktreePath, const QString &prefix)
{
    return prefix.isEmpty() ? worktreePath : QDir::cleanPath(QDir(worktreePath).filePath(prefix));
}

struct Context
{
    QString anchorId;
    QVariantMap anchor;
    QString anchorRoot;
    GitWorktrees::Repository repository;
};

Context resolve(CoreDatabase &database, const QString &libraryId, bool requireRepository)
{
    Context context;
    requireLibrary(database, libraryId);
    context.anchorId = WorktreeRepository::anchorLibraryId(database, libraryId);
    context.anchor = requireLibrary(database, context.anchorId);
    context.anchorRoot = context.anchor.value(QStringLiteral("rootPath")).toString();
    context.repository = GitWorktrees::inspect(context.anchorRoot);

    if (requireRepository && !context.repository.repository) {
        if (!QFileInfo(context.anchorRoot).isDir()) {
            fail(QStringLiteral("LIBRARY_MISSING"), QStringLiteral("The Library folder %1 is missing.").arg(context.anchorRoot));
        }

        fail(QStringLiteral("NOT_A_REPOSITORY"), QStringLiteral("This Library is not inside a Git repository."));
    }

    return context;
}

const GitWorktrees::Worktree *findWorktree(const QList<GitWorktrees::Worktree> &worktrees, const QString &path)
{
    for (const GitWorktrees::Worktree &worktree : worktrees) {
        if (worktree.path == path || GitWorktrees::samePath(worktree.path, path)) {
            return &worktree;
        }
    }

    return nullptr;
}

QString worktreeLabel(const GitWorktrees::Worktree &worktree)
{
    if (!worktree.branch.isEmpty()) {
        return worktree.branch;
    }

    if (!worktree.head.isEmpty()) {
        return worktree.head.left(8);
    }

    return QFileInfo(worktree.path).fileName();
}

QString worktreeLibraryName(const QVariantMap &anchor, const GitWorktrees::Worktree &worktree)
{
    const QString name = anchor.value(QStringLiteral("name")).toString() + QStringLiteral(" · ") + worktreeLabel(worktree);
    return name.left(120);
}

// The Library (by id) that holds this worktree, if any: a linked one, or one
// whose root already is the worktree's mirror of the anchor's folder.
QVariantMap libraryForWorktree(CoreDatabase &database, const QString &anchorId, const QString &worktreePath, const QString &libraryRoot)
{
    const auto linked = database
        .prepare(QStringLiteral("SELECT library_id FROM library_worktrees WHERE anchor_library_id = ? AND worktree_path = ?"))
        .bind(1, anchorId)
        .bind(2, worktreePath)
        .one();

    if (linked) {
        const auto library = LibraryRepository::libraryById(database, linked->value(QStringLiteral("library_id")).toString());

        if (library) {
            return *library;
        }
    }

    const QString canonicalRoot = GitWorktrees::canonicalPath(libraryRoot);
    const auto byRoot = database.prepare(QStringLiteral("SELECT id FROM libraries WHERE root_path = ?")).bind(1, canonicalRoot).one();

    if (byRoot) {
        const auto library = LibraryRepository::libraryById(database, byRoot->value(QStringLiteral("id")).toString());

        if (library) {
            return *library;
        }
    }

    return {};
}

void appendJournal(CoreDatabase &database, const QString &libraryId, const QString &kind, const QVariantMap &details)
{
    WorkspaceChangeJournal::Event event;
    event.kind = kind;
    event.evidence = QStringLiteral("performed");
    event.actorType = QStringLiteral("user");
    event.details = details;
    WorkspaceChangeJournal::append(database, libraryId, event);
}

QVariantMap blocker(const QString &code, const QString &message)
{
    return {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
}
}

namespace WorktreeRepository
{
QString anchorLibraryId(CoreDatabase &database, const QString &libraryId)
{
    const QVariant anchor = database
        .prepare(QStringLiteral("SELECT anchor_library_id FROM library_worktrees WHERE library_id = ?"))
        .bind(1, libraryId)
        .scalar();

    return isNullish(anchor) ? libraryId : anchor.toString();
}

std::optional<QVariantMap> linkFor(CoreDatabase &database, const QString &libraryId)
{
    const auto row = database
        .prepare(QStringLiteral("SELECT %1 FROM library_worktrees WHERE library_id = ?").arg(linkColumns))
        .bind(1, libraryId)
        .one();

    return row ? std::optional<QVariantMap>(mapLink(*row)) : std::nullopt;
}

QVariantMap runningRunsByLibrary(CoreDatabase &database, const QStringList &libraryIds)
{
    QVariantMap result;
    const QStringList ids = uniqueIds(libraryIds);

    if (ids.isEmpty()) {
        return result;
    }

    auto statement = database.prepare(QStringLiteral(
        "SELECT ai_runs.id, ai_runs.library_id, ai_runs.chat_id, ai_runs.agent_id, agents.name AS agent_name, "
        "ai_runs.phase, ai_runs.started_at FROM ai_runs LEFT JOIN agents ON agents.id = ai_runs.agent_id "
        "WHERE ai_runs.status = 'running' AND ai_runs.library_id IN (%1) ORDER BY ai_runs.started_at ASC"
    ).arg(placeholders(ids.size())));

    for (qsizetype index = 0; index < ids.size(); ++index) {
        statement.bind(int(index) + 1, ids.at(index));
    }

    for (const QVariantMap &row : statement.all()) {
        const QString libraryId = row.value(QStringLiteral("library_id")).toString();
        QVariantList runs = result.value(libraryId).toList();
        runs.append(QVariantMap{
            {QStringLiteral("runId"), row.value(QStringLiteral("id"))},
            {QStringLiteral("chatId"), row.value(QStringLiteral("chat_id"))},
            {QStringLiteral("agentId"), row.value(QStringLiteral("agent_id"))},
            {QStringLiteral("agentName"), nullable(row.value(QStringLiteral("agent_name")))},
            {QStringLiteral("phase"), row.value(QStringLiteral("phase"))},
            {QStringLiteral("startedAt"), row.value(QStringLiteral("started_at"))},
        });
        result.insert(libraryId, runs);
    }

    return result;
}

QVariantMap overview(CoreDatabase &database, const QString &libraryId)
{
    const Context context = resolve(database, libraryId, false);
    QVariantMap result{
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("anchorLibraryId"), context.anchorId},
        {QStringLiteral("repository"), QVariant::fromValue(nullptr)},
        {QStringLiteral("worktrees"), QVariantList{}},
        {QStringLiteral("error"), QVariant::fromValue(nullptr)},
        {QStringLiteral("refreshedAt"), JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc())},
    };

    if (!context.repository.repository) {
        if (!QFileInfo(context.anchorRoot).isDir()) {
            result.insert(QStringLiteral("error"), QStringLiteral("The Library folder %1 is missing.").arg(context.anchorRoot));
        }

        return result;
    }

    QList<GitWorktrees::Worktree> worktrees;

    try {
        worktrees = GitWorktrees::list(context.anchorRoot);
    } catch (const CoreError &error) {
        result.insert(QStringLiteral("error"), error.message());
        return result;
    }

    const QString prefix = context.repository.prefix;
    const QString mainPath = worktrees.isEmpty() ? context.repository.topLevel : worktrees.first().path;
    result.insert(QStringLiteral("repository"), QVariantMap{
        {QStringLiteral("key"), context.repository.key},
        {QStringLiteral("name"), QFileInfo(mainPath).fileName()},
        {QStringLiteral("mainPath"), mainPath},
        {QStringLiteral("prefix"), prefix},
    });

    QList<QVariantMap> links;

    for (const QVariantMap &row : database
             .prepare(QStringLiteral("SELECT %1 FROM library_worktrees WHERE anchor_library_id = ? ORDER BY created_at ASC").arg(linkColumns))
             .bind(1, context.anchorId)
             .all()) {
        links.append(mapLink(row));
    }

    QList<QVariantMap> entries;
    QStringList libraryIds;
    QSet<QString> matchedLinks;

    for (const GitWorktrees::Worktree &worktree : worktrees) {
        if (worktree.bare) {
            continue;
        }

        QVariantMap entry = worktree.toMap();
        const QString libraryRoot = joinPrefix(worktree.path, prefix);
        const bool anchor = GitWorktrees::samePath(worktree.path, context.repository.topLevel);
        QVariantMap library = anchor ? context.anchor : libraryForWorktree(database, context.anchorId, worktree.path, libraryRoot);
        const QString worktreeLibraryId = library.value(QStringLiteral("id")).toString();
        QVariant origin = QVariant::fromValue(nullptr);
        bool linked = false;

        for (const QVariantMap &link : links) {
            if (link.value(QStringLiteral("libraryId")).toString() == worktreeLibraryId && !worktreeLibraryId.isEmpty()) {
                linked = true;
                origin = link.value(QStringLiteral("origin"));
                matchedLinks.insert(worktreeLibraryId);
            }
        }

        entry.insert(QStringLiteral("id"), worktree.path);
        entry.insert(QStringLiteral("label"), worktreeLabel(worktree));
        entry.insert(QStringLiteral("libraryId"), nullableString(worktreeLibraryId));
        entry.insert(QStringLiteral("libraryName"), nullableString(library.value(QStringLiteral("name")).toString()));
        entry.insert(QStringLiteral("libraryRoot"), libraryRoot);
        entry.insert(QStringLiteral("libraryArchived"), !isNullish(library.value(QStringLiteral("archivedAt"))));
        entry.insert(QStringLiteral("anchor"), anchor);
        entry.insert(QStringLiteral("linked"), linked);
        entry.insert(QStringLiteral("origin"), origin);
        entry.insert(QStringLiteral("orphaned"), false);
        entries.append(entry);

        if (!worktreeLibraryId.isEmpty()) {
            libraryIds.append(worktreeLibraryId);
        }
    }

    // Opened in Archivist, but git no longer lists them (removed or pruned
    // outside Archivist). Shown so the user can forget them deliberately.
    for (const QVariantMap &link : links) {
        const QString linkLibraryId = link.value(QStringLiteral("libraryId")).toString();

        if (matchedLinks.contains(linkLibraryId)) {
            continue;
        }

        const auto library = LibraryRepository::libraryById(database, linkLibraryId);

        if (!library || !isNullish(library->value(QStringLiteral("archivedAt")))) {
            continue;
        }

        const QString path = link.value(QStringLiteral("worktreePath")).toString();
        entries.append(QVariantMap{
            {QStringLiteral("id"), path},
            {QStringLiteral("path"), path},
            {QStringLiteral("label"), library->value(QStringLiteral("name"))},
            {QStringLiteral("head"), QVariant::fromValue(nullptr)},
            {QStringLiteral("branch"), link.value(QStringLiteral("createdBranch"))},
            {QStringLiteral("detached"), false},
            {QStringLiteral("bare"), false},
            {QStringLiteral("main"), false},
            {QStringLiteral("locked"), false},
            {QStringLiteral("lockReason"), QVariant::fromValue(nullptr)},
            {QStringLiteral("prunable"), false},
            {QStringLiteral("prunableReason"), QVariant::fromValue(nullptr)},
            {QStringLiteral("exists"), QFileInfo(path).isDir()},
            {QStringLiteral("libraryId"), linkLibraryId},
            {QStringLiteral("libraryName"), library->value(QStringLiteral("name"))},
            {QStringLiteral("libraryRoot"), library->value(QStringLiteral("rootPath"))},
            {QStringLiteral("libraryArchived"), false},
            {QStringLiteral("anchor"), false},
            {QStringLiteral("linked"), true},
            {QStringLiteral("origin"), link.value(QStringLiteral("origin"))},
            {QStringLiteral("orphaned"), true},
        });
        libraryIds.append(linkLibraryId);
    }

    const QVariantMap running = runningRunsByLibrary(database, libraryIds);
    QVariantList list;

    for (QVariantMap &entry : entries) {
        const QString entryLibraryId = entry.value(QStringLiteral("libraryId")).toString();
        entry.insert(QStringLiteral("runningRuns"), running.value(entryLibraryId).toList());
        list.append(entry);
    }

    result.insert(QStringLiteral("worktrees"), list);
    return result;
}

QVariantMap statuses(const QStringList &worktreePaths)
{
    QVariantMap result;

    for (const QString &path : worktreePaths) {
        if (!path.isEmpty() && !result.contains(path)) {
            result.insert(path, GitWorktrees::status(path).toMap());
        }
    }

    return result;
}

QVariantMap openWorktree(CoreDatabase &database, const QString &libraryId, const QString &worktreePath)
{
    const Context context = resolve(database, libraryId, true);
    const QList<GitWorktrees::Worktree> worktrees = GitWorktrees::list(context.anchorRoot);
    const GitWorktrees::Worktree *target = findWorktree(worktrees, worktreePath);

    if (!target) {
        fail(
            QStringLiteral("NOT_A_WORKTREE"),
            QStringLiteral("%1 is not a worktree of this repository. Add it with git first, or open it as a Library.").arg(worktreePath)
        );
    }

    if (target->bare) {
        fail(QStringLiteral("BARE_WORKTREE"), QStringLiteral("A bare repository has no files to open."));
    }

    if (!target->exists) {
        fail(QStringLiteral("WORKTREE_MISSING"), QStringLiteral("The worktree folder %1 is missing.").arg(target->path));
    }

    if (GitWorktrees::samePath(target->path, context.repository.topLevel)) {
        return {
            {QStringLiteral("library"), context.anchor},
            {QStringLiteral("created"), false},
            {QStringLiteral("worktree"), target->toMap()},
        };
    }

    const QString libraryRoot = joinPrefix(target->path, context.repository.prefix);

    if (!QFileInfo(libraryRoot).isDir()) {
        fail(
            QStringLiteral("PREFIX_MISSING"),
            QStringLiteral("This worktree has no %1 folder, so there is nothing to open for this Library.").arg(context.repository.prefix)
        );
    }

    const QString rootPath = LibraryRepository::normalizeRootPath(libraryRoot);
    const GitWorktrees::Worktree worktree = *target;

    return database.transaction([&]() {
        bool created = false;
        QString worktreeLibraryId;
        const auto existing = database.prepare(QStringLiteral("SELECT id, archived_at FROM libraries WHERE root_path = ?"))
                                  .bind(1, rootPath)
                                  .one();

        if (existing) {
            worktreeLibraryId = existing->value(QStringLiteral("id")).toString();

            if (!isNullish(existing->value(QStringLiteral("archived_at")))) {
                LibraryRepository::restoreLibrary(database, worktreeLibraryId);
            }
        } else {
            const QVariantMap result = LibraryRepository::createLibrary(database, {
                {QStringLiteral("rootPath"), rootPath},
                {QStringLiteral("name"), worktreeLibraryName(context.anchor, worktree)},
            });
            worktreeLibraryId = result.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
            created = true;
        }

        // A Library other worktrees already hang off stays their anchor.
        const bool anchorsOthers = database
            .prepare(QStringLiteral("SELECT COUNT(*) FROM library_worktrees WHERE anchor_library_id = ?"))
            .bind(1, worktreeLibraryId)
            .scalar()
            .toInt() > 0;
        const bool alreadyLinked = linkFor(database, worktreeLibraryId).has_value();

        if (!anchorsOthers && worktreeLibraryId != context.anchorId) {
            database
                .prepare(QStringLiteral(
                    "INSERT INTO library_worktrees (library_id, anchor_library_id, repository_key, worktree_path, "
                    "library_prefix, origin) VALUES (?, ?, ?, ?, ?, 'opened') "
                    "ON CONFLICT(library_id) DO UPDATE SET anchor_library_id = excluded.anchor_library_id, "
                    "repository_key = excluded.repository_key, worktree_path = excluded.worktree_path, "
                    "library_prefix = excluded.library_prefix, updated_at = %1"
                ).arg(nowSql))
                .bind(1, worktreeLibraryId)
                .bind(2, context.anchorId)
                .bind(3, context.repository.key)
                .bind(4, worktree.path)
                .bind(5, context.repository.prefix)
                .run();

            if (!alreadyLinked) {
                appendJournal(database, worktreeLibraryId, QStringLiteral("worktree.opened"), {
                    {QStringLiteral("worktreePath"), worktree.path},
                    {QStringLiteral("branch"), nullableString(worktree.branch)},
                    {QStringLiteral("head"), nullableString(worktree.head)},
                    {QStringLiteral("anchorLibraryId"), context.anchorId},
                });
            }
        }

        const auto library = LibraryRepository::libraryById(database, worktreeLibraryId);

        if (!library) {
            throw CoreError(QStringLiteral("The worktree's Library could not be loaded."));
        }

        return QVariantMap{
            {QStringLiteral("library"), *library},
            {QStringLiteral("created"), created},
            {QStringLiteral("worktree"), worktree.toMap()},
        };
    });
}

QVariantMap createWorktree(CoreDatabase &database, const QString &libraryId, const QVariantMap &input)
{
    const Context context = resolve(database, libraryId, true);
    GitWorktrees::CreateOptions options;
    options.branch = input.value(QStringLiteral("branch")).toString();
    options.newBranch = !input.contains(QStringLiteral("newBranch")) || input.value(QStringLiteral("newBranch")).toBool();
    options.baseRef = input.value(QStringLiteral("baseRef")).toString();
    options.path = input.value(QStringLiteral("path")).toString();

    const GitWorktrees::Worktree worktree = GitWorktrees::create(context.anchorRoot, options);
    QVariantMap opened;

    try {
        opened = openWorktree(database, context.anchorId, worktree.path);
    } catch (const CoreError &error) {
        QVariantMap details = error.details();
        details.insert(QStringLiteral("worktreePath"), worktree.path);
        throw CoreError(
            QStringLiteral("Git created the worktree at %1, but Archivist couldn't open it: %2").arg(worktree.path, error.message()),
            details
        );
    }

    const QString worktreeLibraryId = opened.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();

    database.transaction([&]() {
        database
            .prepare(QStringLiteral(
                "UPDATE library_worktrees SET origin = 'created', created_branch = ?, base_ref = ?, updated_at = %1 "
                "WHERE library_id = ?"
            ).arg(nowSql))
            .bind(1, options.branch.trimmed())
            .bind(2, options.newBranch ? nullableString(options.baseRef.trimmed()) : QVariant())
            .bind(3, worktreeLibraryId)
            .run();

        appendJournal(database, worktreeLibraryId, QStringLiteral("worktree.created"), {
            {QStringLiteral("worktreePath"), worktree.path},
            {QStringLiteral("branch"), options.branch.trimmed()},
            {QStringLiteral("newBranch"), options.newBranch},
            {QStringLiteral("baseRef"), nullableString(options.baseRef.trimmed())},
            {QStringLiteral("head"), nullableString(worktree.head)},
            {QStringLiteral("anchorLibraryId"), context.anchorId},
        });
    });

    if (const auto library = LibraryRepository::libraryById(database, worktreeLibraryId)) {
        opened.insert(QStringLiteral("library"), *library);
    }

    return opened;
}

QVariantMap removalPreview(CoreDatabase &database, const QString &libraryId, const QString &worktreePath, int unsavedDocuments)
{
    const Context context = resolve(database, libraryId, true);
    const GitWorktrees::RemovalCheck check = GitWorktrees::removalCheck(context.anchorRoot, worktreePath);
    QVariantList blockers = check.blockers;

    if (GitWorktrees::samePath(worktreePath, context.repository.topLevel)) {
        blockers.prepend(blocker(
            QStringLiteral("ANCHOR_CHECKOUT"),
            QStringLiteral("%1 lives in this checkout. Remove other worktrees from here instead.")
                .arg(context.anchor.value(QStringLiteral("name")).toString())
        ));
    }

    const QVariantMap library = libraryForWorktree(
        database,
        context.anchorId,
        GitWorktrees::canonicalPath(worktreePath),
        joinPrefix(worktreePath, context.repository.prefix)
    );
    QString worktreeLibraryId = library.value(QStringLiteral("id")).toString();

    if (worktreeLibraryId.isEmpty()) {
        const auto linked = database
            .prepare(QStringLiteral("SELECT library_id FROM library_worktrees WHERE anchor_library_id = ? AND worktree_path = ?"))
            .bind(1, context.anchorId)
            .bind(2, worktreePath)
            .scalar();
        worktreeLibraryId = isNullish(linked) ? QString() : linked.toString();
    }

    if (!worktreeLibraryId.isEmpty()) {
        const QVariantList runs = runningRunsByLibrary(database, {worktreeLibraryId}).value(worktreeLibraryId).toList();

        if (!runs.isEmpty()) {
            blockers.append(blocker(
                QStringLiteral("AGENT_RUNNING"),
                QStringLiteral("%1 AI run(s) are still working in this worktree.").arg(runs.size())
            ));
        }
    }

    if (unsavedDocuments > 0) {
        blockers.append(blocker(
            QStringLiteral("UNSAVED_EDITORS"),
            QStringLiteral("%1 open editor(s) have unsaved changes in this worktree.").arg(unsavedDocuments)
        ));
    }

    return {
        {QStringLiteral("allowed"), blockers.isEmpty()},
        {QStringLiteral("blockers"), blockers},
        {QStringLiteral("status"), check.status.toMap()},
        {QStringLiteral("libraryId"), nullableString(worktreeLibraryId)},
        {QStringLiteral("path"), worktreePath},
    };
}

QVariantMap removeWorktree(CoreDatabase &database, const QString &libraryId, const QString &worktreePath, int unsavedDocuments)
{
    const QVariantMap preview = removalPreview(database, libraryId, worktreePath, unsavedDocuments);

    if (!preview.value(QStringLiteral("allowed")).toBool()) {
        QStringList messages;

        for (const QVariant &value : preview.value(QStringLiteral("blockers")).toList()) {
            messages.append(value.toMap().value(QStringLiteral("message")).toString());
        }

        fail(
            QStringLiteral("REMOVAL_BLOCKED"),
            messages.join(QLatin1Char(' ')),
            {{QStringLiteral("blockers"), preview.value(QStringLiteral("blockers"))}}
        );
    }

    const Context context = resolve(database, libraryId, true);
    GitWorktrees::remove(context.anchorRoot, worktreePath);

    const QString worktreeLibraryId = isNullish(preview.value(QStringLiteral("libraryId")))
        ? QString()
        : preview.value(QStringLiteral("libraryId")).toString();

    if (!worktreeLibraryId.isEmpty()) {
        database.transaction([&]() {
            appendJournal(database, worktreeLibraryId, QStringLiteral("worktree.removed"), {
                {QStringLiteral("worktreePath"), worktreePath},
                {QStringLiteral("anchorLibraryId"), context.anchorId},
            });
            database.prepare(QStringLiteral("DELETE FROM library_worktrees WHERE library_id = ?")).bind(1, worktreeLibraryId).run();

            const auto library = LibraryRepository::libraryById(database, worktreeLibraryId);

            if (library && isNullish(library->value(QStringLiteral("archivedAt")))) {
                LibraryRepository::archiveLibrary(database, worktreeLibraryId);
            }
        });
    }

    return {
        {QStringLiteral("removed"), true},
        {QStringLiteral("path"), worktreePath},
        {QStringLiteral("archivedLibraryId"), nullableString(worktreeLibraryId)},
    };
}

QVariantMap forgetWorktree(CoreDatabase &database, const QString &worktreeLibraryId)
{
    const auto link = linkFor(database, worktreeLibraryId);

    if (!link) {
        fail(QStringLiteral("NOT_LINKED"), QStringLiteral("This Library isn't a worktree Archivist opened."));
    }

    const QString path = link->value(QStringLiteral("worktreePath")).toString();
    const QString anchorId = link->value(QStringLiteral("anchorLibraryId")).toString();
    const auto anchor = LibraryRepository::libraryById(database, anchorId);

    if (QFileInfo(path).isDir() && anchor) {
        try {
            if (findWorktree(GitWorktrees::list(anchor->value(QStringLiteral("rootPath")).toString()), path)) {
                fail(
                    QStringLiteral("WORKTREE_STILL_PRESENT"),
                    QStringLiteral("Git still has this worktree. Remove it instead of forgetting it.")
                );
            }
        } catch (const CoreError &error) {
            if (error.details().value(QStringLiteral("code")).toString() == QStringLiteral("WORKTREE_STILL_PRESENT")) {
                throw;
            }
        }
    }

    database.transaction([&]() {
        appendJournal(database, worktreeLibraryId, QStringLiteral("worktree.removed"), {
            {QStringLiteral("worktreePath"), path},
            {QStringLiteral("anchorLibraryId"), anchorId},
            {QStringLiteral("forgotten"), true},
        });
        database.prepare(QStringLiteral("DELETE FROM library_worktrees WHERE library_id = ?")).bind(1, worktreeLibraryId).run();
        const auto library = LibraryRepository::libraryById(database, worktreeLibraryId);

        if (library && isNullish(library->value(QStringLiteral("archivedAt")))) {
            LibraryRepository::archiveLibrary(database, worktreeLibraryId);
        }
    });

    return {
        {QStringLiteral("forgotten"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("archivedLibraryId"), worktreeLibraryId},
    };
}

QVariantMap executionContext(const QString &libraryRoot)
{
    QVariantMap context{
        {QStringLiteral("workspaceRoot"), libraryRoot},
        {QStringLiteral("repositoryKey"), QString()},
        {QStringLiteral("branch"), QString()},
        {QStringLiteral("head"), QString()},
    };

    if (libraryRoot.isEmpty() || !QFileInfo(libraryRoot).isDir()) {
        return context;
    }

    const GitWorktrees::CommandResult details = GitWorktrees::run(libraryRoot, {
        QStringLiteral("rev-parse"),
        QStringLiteral("--path-format=absolute"),
        QStringLiteral("--git-common-dir"),
        QStringLiteral("HEAD"),
    }, 10000);
    const QStringList lines = details.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    if (!lines.isEmpty()) {
        const QString commonDirectory = lines.first().trimmed();

        if (QFileInfo(commonDirectory).isDir()) {
            context.insert(QStringLiteral("repositoryKey"), GitWorktrees::canonicalPath(commonDirectory));
        }

        if (details.ok() && lines.size() >= 2) {
            context.insert(QStringLiteral("head"), lines.at(1).trimmed());
        }
    }

    const GitWorktrees::CommandResult branch = GitWorktrees::run(libraryRoot, {
        QStringLiteral("symbolic-ref"),
        QStringLiteral("--short"),
        QStringLiteral("-q"),
        QStringLiteral("HEAD"),
    }, 10000);

    if (branch.ok()) {
        context.insert(QStringLiteral("branch"), branch.output.trimmed());
    }

    return context;
}
}
