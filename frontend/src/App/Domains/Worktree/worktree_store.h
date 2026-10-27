#pragma once

#include "Core/Domains/Worktrees/workspace_change_journal.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// QML-facing state for the worktree selector at the bottom of the side panel.
//
// It follows the selected Library: the selector lists every worktree of that
// Library's repository, whichever of them is selected. Selecting a worktree
// selects its Library (LibraryStore), which moves the Explorer, Git status,
// editor tabs and terminals; running agents and terminals in other worktrees
// are untouched because they are bound to their own Library.
//
// Freshness: the list and each worktree's status carry the time they were
// read. A cached list shows instantly when switching between worktrees of the
// same repository and is marked `stale` until git answers again. Status is
// read with one `git status` per worktree, never a filesystem walk.
//
// It also feeds the workspace change journal: run checkpoints when AI runs
// start and settle, and observed changes (from git status snapshots) for the
// selected worktree and worktrees with running agents.
class WorktreeStore final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString libraryId READ libraryId WRITE setLibraryId NOTIFY libraryIdChanged)
    Q_PROPERTY(QString anchorLibraryId READ anchorLibraryId NOTIFY overviewChanged)
    Q_PROPERTY(bool repository READ repository NOTIFY overviewChanged)
    Q_PROPERTY(QVariantMap repositoryInfo READ repositoryInfo NOTIFY overviewChanged)
    Q_PROPERTY(QVariantList worktrees READ worktrees NOTIFY worktreesChanged)
    Q_PROPERTY(QVariantMap activeWorktree READ activeWorktree NOTIFY worktreesChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY overviewChanged)
    Q_PROPERTY(QString refreshedAt READ refreshedAt NOTIFY overviewChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool refreshingStatus READ refreshingStatus NOTIFY refreshingStatusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyAction READ busyAction NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QVariantList branches READ branches NOTIFY branchesChanged)
    Q_PROPERTY(QVariantMap removalPreview READ removalPreview NOTIFY removalPreviewChanged)
    Q_PROPERTY(int unsavedDocumentCount READ unsavedDocumentCount NOTIFY unsavedDocumentsChanged)

public:
    explicit WorktreeStore(QObject *parent = nullptr);

    [[nodiscard]] QString libraryId() const;
    void setLibraryId(const QString &libraryId);
    [[nodiscard]] QString anchorLibraryId() const;
    [[nodiscard]] bool repository() const;
    [[nodiscard]] QVariantMap repositoryInfo() const;
    [[nodiscard]] QVariantList worktrees() const;
    [[nodiscard]] QVariantMap activeWorktree() const;
    [[nodiscard]] bool stale() const;
    [[nodiscard]] QString refreshedAt() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] bool refreshingStatus() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString busyAction() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QVariantList branches() const;
    [[nodiscard]] QVariantMap removalPreview() const;
    [[nodiscard]] int unsavedDocumentCount() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void loadBranches();
    // Where a new worktree for `branch` goes by default (no git call).
    Q_INVOKABLE QString suggestedPath(const QString &branch) const;
    Q_INVOKABLE void createWorktree(const QString &branch, bool newBranch, const QString &baseRef, const QString &path);
    Q_INVOKABLE void openWorktree(const QString &worktreePath);
    Q_INVOKABLE void previewRemoval(const QString &worktreePath);
    Q_INVOKABLE void clearRemovalPreview();
    Q_INVOKABLE void removeWorktree(const QString &worktreePath);
    Q_INVOKABLE void forgetWorktree(const QString &worktreeLibraryId);
    Q_INVOKABLE void clearError();

    // Editors report unsaved state here so removal and the selector can see
    // unsaved work in worktrees that are not on screen.
    Q_INVOKABLE void setDocumentDirty(const QString &documentId, const QString &libraryId, bool dirty);
    Q_INVOKABLE bool isDocumentDirty(const QString &documentId) const;
    Q_INVOKABLE int unsavedDocumentsIn(const QString &libraryId) const;

    // AIRuntime::runEvent, for checkpoints and running-agent state.
    void handleRunEvent(const QVariantMap &event);
    // LibraryStore::gitStatusChanged for the selected Library: something on
    // disk or in git moved, so observe it.
    void noteSelectedWorkspaceChanged();

signals:
    void libraryIdChanged();
    void overviewChanged();
    void worktreesChanged();
    void loadingChanged();
    void refreshingStatusChanged();
    void busyChanged();
    void errorMessageChanged();
    void branchesChanged();
    void removalPreviewChanged();
    void unsavedDocumentsChanged();
    // A worktree's Library is ready to select. `created` means its catalog is
    // empty and needs a first scan.
    void worktreeOpened(const QVariantMap &library, bool created);
    void worktreeRemoved(const QString &worktreePath, const QString &archivedLibraryId);
    void librariesChanged();

private:
    struct ObservedWorkspace
    {
        WorkspaceChangeJournal::Snapshot snapshot;
        qint64 journalSequence = 0;
        bool capturing = false;
    };

    void loadOverview(bool withStatus);
    void applyOverview(const QVariantMap &overview);
    void rebuildWorktrees();
    void setLoading(bool loading);
    void setRefreshingStatus(bool refreshing);
    void setBusy(const QString &action);
    void setErrorMessage(const QString &message);
    void setRemovalPreview(const QVariantMap &preview);
    void scheduleObservation();
    void observe(const QStringList &libraryIds);
    void recordRunCheckpoint(const QString &runId, const QString &eventType);
    [[nodiscard]] QStringList observedLibraryIds() const;
    [[nodiscard]] bool hasRunningRuns() const;

    QString m_libraryId;
    QString m_anchorLibraryId;
    QVariantMap m_overview;
    QHash<QString, QVariantMap> m_overviewByAnchor;
    QHash<QString, QString> m_anchorByLibrary;
    QVariantMap m_statusByPath;
    QHash<QString, QString> m_statusCheckedAtByPath;
    QVariantList m_worktrees;
    bool m_stale = false;
    bool m_loading = false;
    bool m_refreshingStatus = false;
    QString m_busyAction;
    QString m_errorMessage;
    QVariantList m_branches;
    QVariantMap m_removalPreview;
    quint64 m_overviewRevision = 0;
    quint64 m_statusRevision = 0;
    QHash<QString, QString> m_dirtyDocuments; // documentId -> libraryId
    QHash<QString, ObservedWorkspace> m_observed; // libraryId -> last snapshot
    QTimer m_runActivityTimer;
    QTimer m_observationTimer;
    QTimer m_agentPollTimer;
};
