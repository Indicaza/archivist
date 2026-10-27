#include "workspace_change_journal.h"

#include "Core/Domains/Worktrees/git_worktrees.h"
#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

using namespace CoreValues;

namespace
{
const QString eventColumns = QStringLiteral(
    "sequence, id, library_id, repository_key, occurred_at, kind, evidence, actor_type, actor_id, "
    "run_id, chat_id, proposal_id, path, previous_path, before_hash, after_hash, details_json"
);

QVariantMap mapEvent(const QVariantMap &row)
{
    return {
        {QStringLiteral("sequence"), row.value(QStringLiteral("sequence"))},
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("repositoryKey"), nullable(row.value(QStringLiteral("repository_key")))},
        {QStringLiteral("occurredAt"), row.value(QStringLiteral("occurred_at"))},
        {QStringLiteral("kind"), row.value(QStringLiteral("kind"))},
        {QStringLiteral("evidence"), row.value(QStringLiteral("evidence"))},
        {
            QStringLiteral("actor"),
            QVariantMap{
                {QStringLiteral("type"), row.value(QStringLiteral("actor_type"))},
                {QStringLiteral("id"), nullable(row.value(QStringLiteral("actor_id")))},
            },
        },
        {QStringLiteral("runId"), nullable(row.value(QStringLiteral("run_id")))},
        {QStringLiteral("chatId"), nullable(row.value(QStringLiteral("chat_id")))},
        {QStringLiteral("proposalId"), nullable(row.value(QStringLiteral("proposal_id")))},
        {QStringLiteral("path"), nullable(row.value(QStringLiteral("path")))},
        {QStringLiteral("previousPath"), nullable(row.value(QStringLiteral("previous_path")))},
        {QStringLiteral("beforeHash"), nullable(row.value(QStringLiteral("before_hash")))},
        {QStringLiteral("afterHash"), nullable(row.value(QStringLiteral("after_hash")))},
        {QStringLiteral("details"), JsonValues::parseObject(row.value(QStringLiteral("details_json")).toString())},
    };
}

QVariant optionalText(const QString &value)
{
    return value.isEmpty() ? QVariant() : QVariant(value);
}

int boundedLimit(int limit)
{
    return std::clamp(limit, 1, 5000);
}

QString signatureFor(const QString &absolutePath, QChar code)
{
    const QFileInfo info(absolutePath);

    if (!info.exists()) {
        return QString(code) + QStringLiteral("|absent");
    }

    return QStringLiteral("%1|%2|%3")
        .arg(code)
        .arg(info.isDir() ? -1 : info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

QChar codeOf(const QString &signature)
{
    return signature.isEmpty() ? QChar() : signature.at(0);
}

bool absent(const QString &signature)
{
    return signature.endsWith(QStringLiteral("|absent"));
}

WorkspaceChangeJournal::Event observed(const QString &kind, const QString &path)
{
    WorkspaceChangeJournal::Event event;
    event.kind = kind;
    event.evidence = QStringLiteral("observed");
    event.actorType = QStringLiteral("unknown");
    event.path = path;
    return event;
}
}

namespace WorkspaceChangeJournal
{
bool isKnownKind(const QString &kind)
{
    static const QSet<QString> kinds{
        QStringLiteral("file.created"),
        QStringLiteral("file.modified"),
        QStringLiteral("file.deleted"),
        QStringLiteral("file.renamed"),
        QStringLiteral("directory.created"),
        QStringLiteral("git.head"),
        QStringLiteral("checkpoint"),
        QStringLiteral("run.status"),
        QStringLiteral("worktree.created"),
        QStringLiteral("worktree.opened"),
        QStringLiteral("worktree.removed"),
    };
    return kinds.contains(kind);
}

qint64 append(CoreDatabase &database, const QString &libraryId, const Event &event)
{
    static const QSet<QString> evidence{QStringLiteral("observed"), QStringLiteral("performed"), QStringLiteral("inferred")};
    static const QSet<QString> actors{
        QStringLiteral("user"),
        QStringLiteral("agent"),
        QStringLiteral("external"),
        QStringLiteral("system"),
        QStringLiteral("unknown"),
    };

    if (!isKnownKind(event.kind)) {
        throw CoreError(QStringLiteral("Unknown workspace change kind \"%1\".").arg(event.kind));
    }

    if (!evidence.contains(event.evidence)) {
        throw CoreError(QStringLiteral("Unknown workspace change evidence \"%1\".").arg(event.evidence));
    }

    if (!actors.contains(event.actorType)) {
        throw CoreError(QStringLiteral("Unknown workspace change actor \"%1\".").arg(event.actorType));
    }

    const QVariant repositoryKey = database
        .prepare(QStringLiteral("SELECT repository_key FROM library_worktrees WHERE library_id = ?"))
        .bind(1, libraryId)
        .scalar();

    auto statement = database.prepare(QStringLiteral(
        "INSERT INTO workspace_change_events (id, library_id, repository_key, occurred_at, kind, evidence, "
        "actor_type, actor_id, run_id, chat_id, proposal_id, path, previous_path, before_hash, after_hash, details_json) "
        "VALUES (?, ?, ?, COALESCE(?, strftime('%Y-%m-%dT%H:%M:%fZ', 'now')), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ));
    statement.bind(1, newId())
        .bind(2, libraryId)
        .bind(3, repositoryKey)
        .bind(4, optionalText(event.occurredAt))
        .bind(5, event.kind)
        .bind(6, event.evidence)
        .bind(7, event.actorType)
        .bind(8, optionalText(event.actorId))
        .bind(9, optionalText(event.runId))
        .bind(10, optionalText(event.chatId))
        .bind(11, optionalText(event.proposalId))
        .bind(12, optionalText(event.path))
        .bind(13, optionalText(event.previousPath))
        .bind(14, optionalText(event.beforeHash))
        .bind(15, optionalText(event.afterHash))
        .bind(16, JsonValues::toJsonString(event.details))
        .run();

    return database.prepare(QStringLiteral("SELECT last_insert_rowid()")).scalar().toLongLong();
}

void appendAll(CoreDatabase &database, const QString &libraryId, const QList<Event> &events)
{
    if (events.isEmpty()) {
        return;
    }

    auto work = [&]() {
        for (const Event &event : events) {
            append(database, libraryId, event);
        }
    };

    if (database.inTransaction()) {
        work();
    } else {
        database.transaction(work);
    }
}

QVariantList eventsAfter(CoreDatabase &database, const QString &libraryId, qint64 afterSequence, int limit)
{
    QVariantList events;

    for (const QVariantMap &row : database
             .prepare(QStringLiteral("SELECT %1 FROM workspace_change_events WHERE library_id = ? AND sequence > ? "
                                     "ORDER BY sequence ASC LIMIT ?")
                          .arg(eventColumns))
             .bind(1, libraryId)
             .bind(2, afterSequence)
             .bind(3, boundedLimit(limit))
             .all()) {
        events.append(mapEvent(row));
    }

    return events;
}

QVariantList eventsForRun(CoreDatabase &database, const QString &runId, qint64 afterSequence, int limit)
{
    QVariantList events;

    for (const QVariantMap &row : database
             .prepare(QStringLiteral("SELECT %1 FROM workspace_change_events WHERE run_id = ? AND sequence > ? "
                                     "ORDER BY sequence ASC LIMIT ?")
                          .arg(eventColumns))
             .bind(1, runId)
             .bind(2, afterSequence)
             .bind(3, boundedLimit(limit))
             .all()) {
        events.append(mapEvent(row));
    }

    return events;
}

qint64 latestSequence(CoreDatabase &database, const QString &libraryId)
{
    return database
        .prepare(QStringLiteral("SELECT COALESCE(MAX(sequence), 0) FROM workspace_change_events WHERE library_id = ?"))
        .bind(1, libraryId)
        .scalar()
        .toLongLong();
}

QString Snapshot::fingerprint() const
{
    QStringList keys = entries.keys();
    std::sort(keys.begin(), keys.end());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(head.toUtf8());
    hash.addData(QByteArrayLiteral("\n"));
    hash.addData(branch.toUtf8());

    for (const QString &key : keys) {
        hash.addData(QByteArrayLiteral("\n"));
        hash.addData(key.toUtf8());
        hash.addData(QByteArrayLiteral("\t"));
        hash.addData(entries.value(key).toUtf8());
    }

    return QString::fromLatin1(hash.result().toHex());
}

Snapshot capture(const QString &libraryRoot)
{
    Snapshot snapshot;
    const GitWorktrees::Repository repository = GitWorktrees::inspect(libraryRoot);

    if (!repository.repository) {
        return snapshot;
    }

    // A Library on a subfolder only needs that subfolder. When the subfolder
    // itself is untracked git would report it as one entry, so list its files.
    const QStringList pathspec = repository.prefix.isEmpty() ? QStringList() : QStringList{repository.prefix};
    GitWorktrees::Status status = GitWorktrees::status(repository.topLevel, pathspec, false);

    if (!repository.prefix.isEmpty()) {
        for (const GitWorktrees::Status::Change &change : status.changes) {
            if (change.code == QLatin1Char('?') && change.path.endsWith(QLatin1Char('/'))
                && (repository.prefix + QLatin1Char('/')).startsWith(change.path)) {
                status = GitWorktrees::status(repository.topLevel, pathspec, true);
                break;
            }
        }
    }

    if (!status.available) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.head = status.head;
    snapshot.branch = status.branch;
    const QString prefix = repository.prefix.isEmpty() ? QString() : repository.prefix + QLatin1Char('/');

    for (const GitWorktrees::Status::Change &change : status.changes) {
        if (!prefix.isEmpty() && !change.path.startsWith(prefix)) {
            continue;
        }

        const QString relative = change.path.mid(prefix.size());

        if (relative.isEmpty()) {
            continue;
        }

        snapshot.entries.insert(relative, signatureFor(QDir(repository.topLevel).filePath(change.path), change.code));
    }

    return snapshot;
}

QList<Event> diff(const Snapshot &before, const Snapshot &after)
{
    QList<Event> events;

    if (!before.available || !after.available) {
        return events;
    }

    const bool headMoved = before.head != after.head || before.branch != after.branch;

    if (headMoved) {
        Event event = observed(QStringLiteral("git.head"), QString());
        event.details = {
            {QStringLiteral("fromHead"), nullableString(before.head)},
            {QStringLiteral("toHead"), nullableString(after.head)},
            {QStringLiteral("fromBranch"), nullableString(before.branch)},
            {QStringLiteral("toBranch"), nullableString(after.branch)},
        };
        events.append(event);
    }

    QStringList paths = after.entries.keys();

    for (const QString &path : before.entries.keys()) {
        if (!after.entries.contains(path)) {
            paths.append(path);
        }
    }

    std::sort(paths.begin(), paths.end());

    for (const QString &path : paths) {
        const bool wasChanged = before.entries.contains(path);
        const bool isChanged = after.entries.contains(path);
        const QString beforeSignature = before.entries.value(path);
        const QString afterSignature = after.entries.value(path);

        // Same size and mtime with a different status letter is an index
        // change (staging, unstaging), not a change to the file.
        if (wasChanged && isChanged && beforeSignature.mid(1) == afterSignature.mid(1)) {
            continue;
        }

        if (isChanged) {
            const QChar code = codeOf(afterSignature);

            if (code == QLatin1Char('D') || absent(afterSignature)) {
                if (!(wasChanged && (codeOf(beforeSignature) == QLatin1Char('D') || absent(beforeSignature)))) {
                    events.append(observed(QStringLiteral("file.deleted"), path));
                }
            } else if (!wasChanged && (code == QLatin1Char('?') || code == QLatin1Char('A'))) {
                events.append(observed(path.endsWith(QLatin1Char('/')) ? QStringLiteral("directory.created") : QStringLiteral("file.created"), path));
            } else if (wasChanged && (codeOf(beforeSignature) == QLatin1Char('D') || absent(beforeSignature))) {
                events.append(observed(QStringLiteral("file.created"), path));
            } else {
                events.append(observed(QStringLiteral("file.modified"), path));
            }

            continue;
        }

        // The path is clean again. With HEAD moved this is usually a commit or
        // checkout, which the git.head event already describes.
        if (headMoved) {
            continue;
        }

        const QChar code = codeOf(beforeSignature);

        if (code == QLatin1Char('?') || code == QLatin1Char('A')) {
            events.append(observed(QStringLiteral("file.deleted"), path));
        } else if (code == QLatin1Char('D')) {
            events.append(observed(QStringLiteral("file.created"), path));
        } else {
            events.append(observed(QStringLiteral("file.modified"), path));
        }
    }

    return events;
}
}
