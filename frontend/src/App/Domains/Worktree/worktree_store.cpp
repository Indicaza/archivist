#include "worktree_store.h"

#include "Core/Domains/AI/ai_run_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Worktrees/git_worktrees.h"
#include "Core/Domains/Worktrees/worktree_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_runtime.h"

#include <QDateTime>
#include <QSet>
#include <QtDebug>

using namespace CoreValues;

namespace
{
const QString coreUnavailableMessage = QStringLiteral("Archivist's data layer is not running. Restart the app.");

QString now()
{
    return JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc());
}

QVariantMap snapshotToVariant(const WorkspaceChangeJournal::Snapshot &snapshot)
{
    QVariantMap entries;

    for (auto iterator = snapshot.entries.constBegin(); iterator != snapshot.entries.constEnd(); ++iterator) {
        entries.insert(iterator.key(), iterator.value());
    }

    return {
        {QStringLiteral("available"), snapshot.available},
        {QStringLiteral("head"), snapshot.head},
        {QStringLiteral("branch"), snapshot.branch},
        {QStringLiteral("entries"), entries},
    };
}

WorkspaceChangeJournal::Snapshot snapshotFromVariant(const QVariantMap &value)
{
    WorkspaceChangeJournal::Snapshot snapshot;
    snapshot.available = value.value(QStringLiteral("available")).toBool();
    snapshot.head = value.value(QStringLiteral("head")).toString();
    snapshot.branch = value.value(QStringLiteral("branch")).toString();
    const QVariantMap entries = value.value(QStringLiteral("entries")).toMap();

    for (auto iterator = entries.constBegin(); iterator != entries.constEnd(); ++iterator) {
        snapshot.entries.insert(iterator.key(), iterator.value().toString());
    }

    return snapshot;
}

QString libraryRoot(CoreDatabase &database, const QString &libraryId)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);
    return library ? library->value(QStringLiteral("rootPath")).toString() : QString();
}

bool isRunBoundary(const QString &eventType)
{
    return eventType == QStringLiteral("run.started") || eventType == QStringLiteral("run.completed")
        || eventType == QStringLiteral("run.failed") || eventType == QStringLiteral("run.cancelled");
}
}

WorktreeStore::WorktreeStore(QObject *parent)
    : QObject(parent)
{
    m_runActivityTimer.setSingleShot(true);
    m_runActivityTimer.setInterval(400);
    connect(&m_runActivityTimer, &QTimer::timeout, this, [this]() { loadOverview(true); });

    m_observationTimer.setSingleShot(true);
    m_observationTimer.setInterval(700);
    connect(&m_observationTimer, &QTimer::timeout, this, [this]() {
        observe(observedLibraryIds());
        refreshStatus();
    });

    // While an agent works in a worktree, look at it every few seconds: its
    // shell and tools change files without telling the UI.
    m_agentPollTimer.setInterval(4000);
    connect(&m_agentPollTimer, &QTimer::timeout, this, [this]() {
        if (!hasRunningRuns()) {
            m_agentPollTimer.stop();
            return;
        }

        observe(observedLibraryIds());
        refreshStatus();
    });
}

QString WorktreeStore::libraryId() const
{
    return m_libraryId;
}

void WorktreeStore::setLibraryId(const QString &libraryId)
{
    if (m_libraryId == libraryId) {
        return;
    }

    m_libraryId = libraryId;
    emit libraryIdChanged();
    setRemovalPreview({});

    if (libraryId.isEmpty()) {
        ++m_overviewRevision;
        m_overview.clear();
        m_anchorLibraryId.clear();
        m_stale = false;
        rebuildWorktrees();
        emit overviewChanged();
        return;
    }

    // Switching between worktrees of one repository: show the list we have
    // right away, marked stale until git confirms it.
    const QString knownAnchor = m_anchorByLibrary.value(libraryId);

    if (!knownAnchor.isEmpty() && m_overviewByAnchor.contains(knownAnchor)) {
        m_overview = m_overviewByAnchor.value(knownAnchor);
        m_anchorLibraryId = knownAnchor;
        m_stale = true;
        rebuildWorktrees();
        emit overviewChanged();
    } else if (knownAnchor != m_anchorLibraryId) {
        m_overview.clear();
        m_anchorLibraryId.clear();
        m_stale = false;
        rebuildWorktrees();
        emit overviewChanged();
    }

    loadOverview(true);
    scheduleObservation();
}

QString WorktreeStore::anchorLibraryId() const
{
    return m_anchorLibraryId;
}

bool WorktreeStore::repository() const
{
    return m_overview.value(QStringLiteral("repository")).typeId() == QMetaType::QVariantMap;
}

QVariantMap WorktreeStore::repositoryInfo() const
{
    return m_overview.value(QStringLiteral("repository")).toMap();
}

QVariantList WorktreeStore::worktrees() const
{
    return m_worktrees;
}

QVariantMap WorktreeStore::activeWorktree() const
{
    for (const QVariant &value : m_worktrees) {
        const QVariantMap entry = value.toMap();

        if (entry.value(QStringLiteral("active")).toBool()) {
            return entry;
        }
    }

    return {};
}

bool WorktreeStore::stale() const
{
    return m_stale;
}

QString WorktreeStore::refreshedAt() const
{
    return m_overview.value(QStringLiteral("refreshedAt")).toString();
}

bool WorktreeStore::loading() const
{
    return m_loading;
}

bool WorktreeStore::refreshingStatus() const
{
    return m_refreshingStatus;
}

bool WorktreeStore::busy() const
{
    return !m_busyAction.isEmpty();
}

QString WorktreeStore::busyAction() const
{
    return m_busyAction;
}

QString WorktreeStore::errorMessage() const
{
    return m_errorMessage;
}

QVariantList WorktreeStore::branches() const
{
    return m_branches;
}

QVariantMap WorktreeStore::removalPreview() const
{
    return m_removalPreview;
}

int WorktreeStore::unsavedDocumentCount() const
{
    return int(m_dirtyDocuments.size());
}

void WorktreeStore::refresh()
{
    loadOverview(true);
}

void WorktreeStore::loadOverview(bool withStatus)
{
    if (m_libraryId.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    const QString requestedLibraryId = m_libraryId;
    const quint64 revision = ++m_overviewRevision;
    setLoading(true);

    core->run(
        [requestedLibraryId](CoreDatabase &database) {
            return QVariant(WorktreeRepository::overview(database, requestedLibraryId));
        },
        this,
        [this, requestedLibraryId, revision, withStatus](const CoreResult &result) {
            if (revision != m_overviewRevision || requestedLibraryId != m_libraryId) {
                return;
            }

            setLoading(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            applyOverview(result.value.toMap());

            if (withStatus) {
                refreshStatus();
            }
        }
    );
}

void WorktreeStore::applyOverview(const QVariantMap &overview)
{
    m_overview = overview;
    m_anchorLibraryId = overview.value(QStringLiteral("anchorLibraryId")).toString();
    m_stale = false;

    const QString overviewError = overview.value(QStringLiteral("error")).toString();

    if (!overviewError.isEmpty()) {
        setErrorMessage(overviewError);
    }

    if (!m_anchorLibraryId.isEmpty()) {
        m_overviewByAnchor.insert(m_anchorLibraryId, overview);
        m_anchorByLibrary.insert(m_anchorLibraryId, m_anchorLibraryId);

        for (const QVariant &value : overview.value(QStringLiteral("worktrees")).toList()) {
            const QString entryLibraryId = value.toMap().value(QStringLiteral("libraryId")).toString();

            if (!entryLibraryId.isEmpty()) {
                m_anchorByLibrary.insert(entryLibraryId, m_anchorLibraryId);
            }
        }
    }

    rebuildWorktrees();
    emit overviewChanged();

    if (hasRunningRuns() && !m_agentPollTimer.isActive()) {
        m_agentPollTimer.start();
    }
}

void WorktreeStore::refreshStatus()
{
    QStringList paths;

    for (const QVariant &value : m_overview.value(QStringLiteral("worktrees")).toList()) {
        const QVariantMap entry = value.toMap();

        if (entry.value(QStringLiteral("exists")).toBool() && !entry.value(QStringLiteral("orphaned")).toBool()) {
            paths.append(entry.value(QStringLiteral("path")).toString());
        }
    }

    if (paths.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        return;
    }

    const quint64 revision = ++m_statusRevision;
    setRefreshingStatus(true);

    core->run(
        [paths](CoreDatabase &) { return QVariant(WorktreeRepository::statuses(paths)); },
        this,
        [this, revision](const CoreResult &result) {
            if (revision != m_statusRevision) {
                return;
            }

            setRefreshingStatus(false);

            if (!result.ok) {
                return;
            }

            const QVariantMap statuses = result.value.toMap();
            const QString checkedAt = now();

            for (auto iterator = statuses.constBegin(); iterator != statuses.constEnd(); ++iterator) {
                m_statusByPath.insert(iterator.key(), iterator.value());
                m_statusCheckedAtByPath.insert(iterator.key(), checkedAt);
            }

            rebuildWorktrees();
        }
    );
}

void WorktreeStore::rebuildWorktrees()
{
    QVariantList next;

    for (const QVariant &value : m_overview.value(QStringLiteral("worktrees")).toList()) {
        QVariantMap entry = value.toMap();
        const QString path = entry.value(QStringLiteral("path")).toString();
        const QString entryLibraryId = entry.value(QStringLiteral("libraryId")).toString();
        const QVariant status = m_statusByPath.value(path);
        const bool hasStatus = status.typeId() == QMetaType::QVariantMap;

        entry.insert(QStringLiteral("status"), hasStatus ? status : QVariant::fromValue(nullptr));
        entry.insert(QStringLiteral("statusCheckedAt"), nullableString(m_statusCheckedAtByPath.value(path)));
        entry.insert(QStringLiteral("dirty"), hasStatus && status.toMap().value(QStringLiteral("dirty")).toBool());
        entry.insert(QStringLiteral("active"), !entryLibraryId.isEmpty() && entryLibraryId == m_libraryId);
        entry.insert(QStringLiteral("unsavedDocuments"), unsavedDocumentsIn(entryLibraryId));
        entry.insert(QStringLiteral("running"), !entry.value(QStringLiteral("runningRuns")).toList().isEmpty());
        next.append(entry);
    }

    if (next == m_worktrees) {
        return;
    }

    m_worktrees = next;
    emit worktreesChanged();
}

void WorktreeStore::loadBranches()
{
    const QString mainPath = repositoryInfo().value(QStringLiteral("mainPath")).toString();
    CoreRuntime *core = CoreRuntime::active();

    if (mainPath.isEmpty() || !core) {
        return;
    }

    core->run(
        [mainPath](CoreDatabase &) { return QVariant(GitWorktrees::localBranches(mainPath)); },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                return;
            }

            QVariantList branches;

            for (const QString &branch : result.value.toStringList()) {
                branches.append(branch);
            }

            if (branches != m_branches) {
                m_branches = branches;
                emit branchesChanged();
            }
        }
    );
}

QString WorktreeStore::suggestedPath(const QString &branch) const
{
    const QString mainPath = repositoryInfo().value(QStringLiteral("mainPath")).toString();

    if (mainPath.isEmpty() || branch.trimmed().isEmpty()) {
        return {};
    }

    return GitWorktrees::suggestedPath(mainPath, branch.trimmed());
}

void WorktreeStore::createWorktree(const QString &branch, bool newBranch, const QString &baseRef, const QString &path)
{
    if (busy() || m_libraryId.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    const QString libraryId = m_libraryId;
    const QVariantMap input{
        {QStringLiteral("branch"), branch.trimmed()},
        {QStringLiteral("newBranch"), newBranch},
        {QStringLiteral("baseRef"), baseRef.trimmed()},
        {QStringLiteral("path"), path.trimmed()},
    };

    setErrorMessage({});
    setBusy(QStringLiteral("create"));

    core->run(
        [libraryId, input](CoreDatabase &database) {
            return QVariant(WorktreeRepository::createWorktree(database, libraryId, input));
        },
        this,
        [this](const CoreResult &result) {
            setBusy({});

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                loadOverview(true);
                return;
            }

            const QVariantMap opened = result.value.toMap();
            emit librariesChanged();
            emit worktreeOpened(opened.value(QStringLiteral("library")).toMap(), true);
            loadOverview(true);
        }
    );
}

void WorktreeStore::openWorktree(const QString &worktreePath)
{
    if (busy() || m_libraryId.isEmpty() || worktreePath.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    const QString libraryId = m_libraryId;
    setErrorMessage({});
    setBusy(QStringLiteral("open"));

    core->run(
        [libraryId, worktreePath](CoreDatabase &database) {
            return QVariant(WorktreeRepository::openWorktree(database, libraryId, worktreePath));
        },
        this,
        [this](const CoreResult &result) {
            setBusy({});

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap opened = result.value.toMap();
            emit librariesChanged();
            emit worktreeOpened(
                opened.value(QStringLiteral("library")).toMap(),
                opened.value(QStringLiteral("created")).toBool()
            );
            loadOverview(true);
        }
    );
}

void WorktreeStore::previewRemoval(const QString &worktreePath)
{
    if (m_libraryId.isEmpty() || worktreePath.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    QString worktreeLibraryId;

    for (const QVariant &value : m_worktrees) {
        const QVariantMap entry = value.toMap();

        if (entry.value(QStringLiteral("path")).toString() == worktreePath) {
            worktreeLibraryId = entry.value(QStringLiteral("libraryId")).toString();
        }
    }

    const QString libraryId = m_libraryId;
    const int unsaved = unsavedDocumentsIn(worktreeLibraryId);
    setRemovalPreview({{QStringLiteral("path"), worktreePath}, {QStringLiteral("loading"), true}});

    core->run(
        [libraryId, worktreePath, unsaved](CoreDatabase &database) {
            return QVariant(WorktreeRepository::removalPreview(database, libraryId, worktreePath, unsaved));
        },
        this,
        [this, worktreePath](const CoreResult &result) {
            if (m_removalPreview.value(QStringLiteral("path")).toString() != worktreePath) {
                return;
            }

            if (!result.ok) {
                setRemovalPreview({
                    {QStringLiteral("path"), worktreePath},
                    {QStringLiteral("allowed"), false},
                    {QStringLiteral("blockers"), QVariantList{QVariantMap{
                        {QStringLiteral("code"), QStringLiteral("PREVIEW_FAILED")},
                        {QStringLiteral("message"), result.errorMessage},
                    }}},
                });
                return;
            }

            QVariantMap preview = result.value.toMap();
            preview.insert(QStringLiteral("loading"), false);
            setRemovalPreview(preview);
        }
    );
}

void WorktreeStore::clearRemovalPreview()
{
    setRemovalPreview({});
}

void WorktreeStore::removeWorktree(const QString &worktreePath)
{
    if (busy() || m_libraryId.isEmpty() || worktreePath.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    QString worktreeLibraryId;

    for (const QVariant &value : m_worktrees) {
        const QVariantMap entry = value.toMap();

        if (entry.value(QStringLiteral("path")).toString() == worktreePath) {
            worktreeLibraryId = entry.value(QStringLiteral("libraryId")).toString();
        }
    }

    // The worktree on screen can't be removed from under the user; the UI
    // switches away first.
    if (!worktreeLibraryId.isEmpty() && worktreeLibraryId == m_libraryId) {
        setErrorMessage(QStringLiteral("Switch to another worktree before removing this one."));
        return;
    }

    const QString libraryId = m_libraryId;
    const int unsaved = unsavedDocumentsIn(worktreeLibraryId);
    setErrorMessage({});
    setBusy(QStringLiteral("remove"));

    core->run(
        [libraryId, worktreePath, unsaved](CoreDatabase &database) {
            return QVariant(WorktreeRepository::removeWorktree(database, libraryId, worktreePath, unsaved));
        },
        this,
        [this, worktreePath](const CoreResult &result) {
            setBusy({});

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                previewRemoval(worktreePath);
                loadOverview(true);
                return;
            }

            const QString archivedLibraryId = result.value.toMap().value(QStringLiteral("archivedLibraryId")).toString();
            m_observed.remove(archivedLibraryId);
            m_statusByPath.remove(worktreePath);
            m_statusCheckedAtByPath.remove(worktreePath);
            setRemovalPreview({});
            emit librariesChanged();
            emit worktreeRemoved(worktreePath, archivedLibraryId);
            loadOverview(true);
        }
    );
}

void WorktreeStore::forgetWorktree(const QString &worktreeLibraryId)
{
    if (busy() || worktreeLibraryId.isEmpty()) {
        return;
    }

    if (worktreeLibraryId == m_libraryId) {
        setErrorMessage(QStringLiteral("Switch to another worktree before forgetting this one."));
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setBusy(QStringLiteral("forget"));

    core->run(
        [worktreeLibraryId](CoreDatabase &database) {
            return QVariant(WorktreeRepository::forgetWorktree(database, worktreeLibraryId));
        },
        this,
        [this, worktreeLibraryId](const CoreResult &result) {
            setBusy({});

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            m_observed.remove(worktreeLibraryId);
            emit librariesChanged();
            emit worktreeRemoved(result.value.toMap().value(QStringLiteral("path")).toString(), worktreeLibraryId);
            loadOverview(true);
        }
    );
}

void WorktreeStore::clearError()
{
    setErrorMessage({});
}

void WorktreeStore::setDocumentDirty(const QString &documentId, const QString &libraryId, bool dirty)
{
    if (documentId.isEmpty()) {
        return;
    }

    const bool known = m_dirtyDocuments.contains(documentId);

    if (dirty == known && (!dirty || m_dirtyDocuments.value(documentId) == libraryId)) {
        return;
    }

    if (dirty) {
        m_dirtyDocuments.insert(documentId, libraryId);
    } else {
        m_dirtyDocuments.remove(documentId);
    }

    emit unsavedDocumentsChanged();
    rebuildWorktrees();
}

bool WorktreeStore::isDocumentDirty(const QString &documentId) const
{
    return m_dirtyDocuments.contains(documentId);
}

int WorktreeStore::unsavedDocumentsIn(const QString &libraryId) const
{
    if (libraryId.isEmpty()) {
        return 0;
    }

    int count = 0;

    for (auto iterator = m_dirtyDocuments.constBegin(); iterator != m_dirtyDocuments.constEnd(); ++iterator) {
        if (iterator.value() == libraryId) {
            ++count;
        }
    }

    return count;
}

void WorktreeStore::handleRunEvent(const QVariantMap &event)
{
    const QString eventType = event.value(QStringLiteral("eventType")).toString();

    if (isRunBoundary(eventType)) {
        recordRunCheckpoint(event.value(QStringLiteral("runId")).toString(), eventType);
        m_runActivityTimer.start();
        return;
    }

    if (eventType == QStringLiteral("edit.completed") || eventType == QStringLiteral("edit.undone")) {
        m_runActivityTimer.start();
    }
}

void WorktreeStore::noteSelectedWorkspaceChanged()
{
    scheduleObservation();
}

void WorktreeStore::scheduleObservation()
{
    m_observationTimer.start();
}

bool WorktreeStore::hasRunningRuns() const
{
    for (const QVariant &value : m_overview.value(QStringLiteral("worktrees")).toList()) {
        if (!value.toMap().value(QStringLiteral("runningRuns")).toList().isEmpty()) {
            return true;
        }
    }

    return false;
}

QStringList WorktreeStore::observedLibraryIds() const
{
    QStringList ids;

    if (!m_libraryId.isEmpty()) {
        ids.append(m_libraryId);
    }

    for (const QVariant &value : m_overview.value(QStringLiteral("worktrees")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString entryLibraryId = entry.value(QStringLiteral("libraryId")).toString();

        if (!entryLibraryId.isEmpty() && !entry.value(QStringLiteral("runningRuns")).toList().isEmpty()
            && !ids.contains(entryLibraryId)) {
            ids.append(entryLibraryId);
        }
    }

    return ids;
}

void WorktreeStore::observe(const QStringList &libraryIds)
{
    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        return;
    }

    // Forget baselines of worktrees nobody watches any more; coming back
    // starts a fresh baseline instead of reporting a burst of old changes.
    for (const QString &known : m_observed.keys()) {
        if (!libraryIds.contains(known) && !m_observed.value(known).capturing) {
            m_observed.remove(known);
        }
    }

    for (const QString &observedLibraryId : libraryIds) {
        ObservedWorkspace &state = m_observed[observedLibraryId];

        if (state.capturing) {
            continue;
        }

        state.capturing = true;
        const bool hasBaseline = state.snapshot.available;
        const QVariantMap previous = snapshotToVariant(state.snapshot);
        const qint64 previousSequence = state.journalSequence;
        QStringList activeRunIds;

        for (const QVariant &value : m_overview.value(QStringLiteral("worktrees")).toList()) {
            const QVariantMap entry = value.toMap();

            if (entry.value(QStringLiteral("libraryId")).toString() == observedLibraryId) {
                for (const QVariant &run : entry.value(QStringLiteral("runningRuns")).toList()) {
                    activeRunIds.append(run.toMap().value(QStringLiteral("runId")).toString());
                }
            }
        }

        core->run(
            [observedLibraryId, hasBaseline, previous, previousSequence, activeRunIds](CoreDatabase &database) {
                const QString root = libraryRoot(database, observedLibraryId);
                const WorkspaceChangeJournal::Snapshot current = WorkspaceChangeJournal::capture(root);
                int recorded = 0;

                if (hasBaseline && current.available) {
                    // Changes Archivist already recorded as performed (editor
                    // saves, Explorer actions, approved AI edits) are not
                    // reported again as anonymous observations.
                    QSet<QString> performedPaths;

                    for (const QVariant &value : WorkspaceChangeJournal::eventsAfter(database, observedLibraryId, previousSequence, 5000)) {
                        const QVariantMap event = value.toMap();

                        if (event.value(QStringLiteral("evidence")).toString() == QStringLiteral("performed")) {
                            performedPaths.insert(event.value(QStringLiteral("path")).toString());
                            performedPaths.insert(event.value(QStringLiteral("previousPath")).toString());
                        }
                    }

                    QList<WorkspaceChangeJournal::Event> events;

                    for (WorkspaceChangeJournal::Event event : WorkspaceChangeJournal::diff(snapshotFromVariant(previous), current)) {
                        if (!event.path.isEmpty() && performedPaths.contains(event.path)) {
                            continue;
                        }

                        // Who did it is unknown; which runs were active is a
                        // fact consumers may use to infer, clearly labelled.
                        if (!activeRunIds.isEmpty()) {
                            event.details.insert(QStringLiteral("activeRunIds"), activeRunIds);
                        }

                        events.append(event);
                    }

                    WorkspaceChangeJournal::appendAll(database, observedLibraryId, events);
                    recorded = int(events.size());
                }

                return QVariant(QVariantMap{
                    {QStringLiteral("snapshot"), snapshotToVariant(current)},
                    {QStringLiteral("sequence"), WorkspaceChangeJournal::latestSequence(database, observedLibraryId)},
                    {QStringLiteral("recorded"), recorded},
                });
            },
            this,
            [this, observedLibraryId](const CoreResult &result) {
                if (!m_observed.contains(observedLibraryId)) {
                    return;
                }

                ObservedWorkspace &state = m_observed[observedLibraryId];
                state.capturing = false;

                if (!result.ok) {
                    qWarning().noquote() << "[Worktrees] observation failed:" << result.errorMessage;
                    return;
                }

                const QVariantMap value = result.value.toMap();
                state.snapshot = snapshotFromVariant(value.value(QStringLiteral("snapshot")).toMap());
                state.journalSequence = value.value(QStringLiteral("sequence")).toLongLong();
            }
        );
    }
}

void WorktreeStore::recordRunCheckpoint(const QString &runId, const QString &eventType)
{
    CoreRuntime *core = CoreRuntime::active();

    if (!core || runId.isEmpty()) {
        return;
    }

    core->run(
        [runId, eventType](CoreDatabase &database) {
            const auto run = AIRunRepository::runById(database, runId);

            if (!run || isNullish(run->value(QStringLiteral("libraryId")))) {
                return QVariant(QString());
            }

            const QString runLibraryId = run->value(QStringLiteral("libraryId")).toString();
            const WorkspaceChangeJournal::Snapshot snapshot = WorkspaceChangeJournal::capture(libraryRoot(database, runLibraryId));

            WorkspaceChangeJournal::Event status;
            status.kind = QStringLiteral("run.status");
            status.evidence = QStringLiteral("performed");
            status.actorType = QStringLiteral("agent");
            status.actorId = run->value(QStringLiteral("agentId")).toString();
            status.runId = runId;
            status.chatId = run->value(QStringLiteral("chatId")).toString();
            status.details = {
                {QStringLiteral("event"), eventType},
                {QStringLiteral("status"), run->value(QStringLiteral("status"))},
                {QStringLiteral("phase"), run->value(QStringLiteral("phase"))},
                {QStringLiteral("workspace"), run->value(QStringLiteral("workspace"))},
            };

            WorkspaceChangeJournal::Event checkpoint;
            checkpoint.kind = QStringLiteral("checkpoint");
            checkpoint.evidence = QStringLiteral("observed");
            checkpoint.actorType = QStringLiteral("system");
            checkpoint.runId = runId;
            checkpoint.chatId = status.chatId;
            checkpoint.details = {
                {QStringLiteral("reason"), eventType},
                {QStringLiteral("available"), snapshot.available},
                {QStringLiteral("head"), nullableString(snapshot.head)},
                {QStringLiteral("branch"), nullableString(snapshot.branch)},
                {QStringLiteral("fingerprint"), snapshot.available ? QVariant(snapshot.fingerprint()) : QVariant::fromValue(nullptr)},
                {QStringLiteral("changedPaths"), QStringList(snapshot.entries.keys())},
            };

            WorkspaceChangeJournal::appendAll(database, runLibraryId, {status, checkpoint});
            return QVariant(runLibraryId);
        },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                qWarning().noquote() << "[Worktrees] run checkpoint not recorded:" << result.errorMessage;
                return;
            }

            scheduleObservation();
        }
    );
}

void WorktreeStore::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }

    m_loading = loading;
    emit loadingChanged();
}

void WorktreeStore::setRefreshingStatus(bool refreshing)
{
    if (m_refreshingStatus == refreshing) {
        return;
    }

    m_refreshingStatus = refreshing;
    emit refreshingStatusChanged();
}

void WorktreeStore::setBusy(const QString &action)
{
    if (m_busyAction == action) {
        return;
    }

    m_busyAction = action;
    emit busyChanged();
}

void WorktreeStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void WorktreeStore::setRemovalPreview(const QVariantMap &preview)
{
    if (m_removalPreview == preview) {
        return;
    }

    m_removalPreview = preview;
    emit removalPreviewChanged();
}
