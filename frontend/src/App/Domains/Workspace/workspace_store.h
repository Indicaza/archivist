#pragma once

#include "App/Domains/Workspace/keyed_list_model.h"
#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_compose.h"
#include "Core/Domains/Workspace/workspace_intents.h"
#include "Core/Domains/Workspace/workspace_model.h"
#include "Core/Domains/Workspace/workspace_preferences.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class DocumentStore;

// QML face of the docking workspace (Core/Domains/Workspace).
//
// Owns one WorkspaceModel per Collection, publishes it as keyed list models
// so QML delegates survive layout changes, runs drags (target, preview,
// commit, cancel) and saves the Collection's snapshot as the layout changes.
//
// Views of documents retain their buffer in DocumentStore, so a buffer lives
// exactly as long as some view shows it (or it has unsaved edits).
class WorkspaceStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString collectionId READ collectionId NOTIFY collectionIdChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString primarySurfaceId READ primarySurfaceId NOTIFY revisionChanged)
    Q_PROPERTY(QStringList surfaceIds READ surfaceIds NOTIFY revisionChanged)
    Q_PROPERTY(QString focusedAnchorId READ focusedAnchorId NOTIFY revisionChanged)
    Q_PROPERTY(QString targetAnchorId READ targetAnchorId NOTIFY revisionChanged)
    Q_PROPERTY(QString explicitTargetAnchorId READ explicitTargetAnchorId NOTIFY revisionChanged)
    Q_PROPERTY(QString focusedViewId READ focusedViewId NOTIFY revisionChanged)
    Q_PROPERTY(QAbstractItemModel *views READ views CONSTANT)
    Q_PROPERTY(bool dragActive READ dragActive NOTIFY dragChanged)
    Q_PROPERTY(QString dragKind READ dragKind NOTIFY dragChanged)
    Q_PROPERTY(QString dragId READ dragId NOTIFY dragChanged)
    Q_PROPERTY(QVariantMap dropTarget READ dropTarget NOTIFY dragChanged)
    Q_PROPERTY(QVariantMap dragLandingRect READ dragLandingRect NOTIFY dragChanged)
    Q_PROPERTY(bool dropAllowed READ dropAllowed NOTIFY dragChanged)
    // The pointer is outside every window: dropping opens a new window.
    Q_PROPERTY(bool dragDetached READ dragDetached NOTIFY dragChanged)
    Q_PROPERTY(QVariantMap detachRect READ detachRect NOTIFY dragChanged)
    Q_PROPERTY(QStringList floatingSurfaceIds READ floatingSurfaceIds NOTIFY surfacesChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QVariantList savedLayouts READ savedLayouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString currentLayoutId READ currentLayoutId NOTIFY layoutsChanged)
    Q_PROPERTY(QString currentLayoutName READ currentLayoutName NOTIFY layoutsChanged)
    Q_PROPERTY(bool canUndoLayout READ canUndoLayout NOTIFY historyChanged)
    // One workspace history for layout switches, agent changes and put
    // back (see "History" below).
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoSummary READ undoSummary NOTIFY historyChanged)
    Q_PROPERTY(QString redoSummary READ redoSummary NOTIFY historyChanged)
    Q_PROPERTY(double minimumAnchorWidth READ minimumAnchorWidth WRITE setMinimumAnchorWidth NOTIFY metricsChanged)
    Q_PROPERTY(double minimumAnchorHeight READ minimumAnchorHeight WRITE setMinimumAnchorHeight NOTIFY metricsChanged)
    Q_PROPERTY(double splitterThickness READ splitterThickness WRITE setSplitterThickness NOTIFY metricsChanged)
    // An arrangement an agent prepared but didn't apply: {id, summary,
    // notes, createdAt} or {}.
    Q_PROPERTY(QVariantMap agentSuggestion READ agentSuggestion NOTIFY agentChanged)
    Q_PROPERTY(bool canUndoAgentChange READ canUndoAgentChange NOTIFY historyChanged)
    // While an agent's arrangement is in place: {since, changes, summary}.
    // Empty once the person keeps it or it is put back.
    Q_PROPERTY(QVariantMap temporaryArrangement READ temporaryArrangement NOTIFY agentChanged)
    // How the person likes the workspace (see workspace_preferences.h):
    // [{id, key, value, anchorId, lasting, duration, sentence, setBy}].
    Q_PROPERTY(QVariantList workspacePreferences READ workspacePreferences NOTIFY preferencesChanged)
    // The bottom Chat/Terminal workbench as the shell reports it:
    // {collapsed, view}.
    Q_PROPERTY(QVariantMap commandSurface READ commandSurface NOTIFY commandSurfaceChanged)

public:
    explicit WorkspaceStore(QObject *parent = nullptr);
    ~WorkspaceStore() override;

    void setDocumentStore(DocumentStore *documents);
    [[nodiscard]] Workspace::ViewTypeRegistry &registry() { return m_registry; }
    [[nodiscard]] const Workspace::WorkspaceModel &model() const { return m_model; }

    [[nodiscard]] QString collectionId() const { return m_collectionId; }
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QString primarySurfaceId() const { return m_model.primarySurfaceId(); }
    [[nodiscard]] QStringList surfaceIds() const { return m_model.surfaceIds(); }
    [[nodiscard]] QString focusedAnchorId() const { return m_model.focusedAnchorId(); }
    [[nodiscard]] QString targetAnchorId() const { return m_model.targetAnchorId(); }
    [[nodiscard]] QString explicitTargetAnchorId() const { return m_model.explicitTargetAnchorId(); }
    [[nodiscard]] QString focusedViewId() const;
    [[nodiscard]] QAbstractItemModel *views() const { return m_views; }
    [[nodiscard]] bool dragActive() const { return m_drag.kind != Workspace::DragSource::Kind::None; }
    [[nodiscard]] QString dragKind() const;
    [[nodiscard]] QString dragId() const { return m_drag.id; }
    [[nodiscard]] QVariantMap dropTarget() const;
    [[nodiscard]] QVariantMap dragLandingRect() const;
    [[nodiscard]] bool dropAllowed() const { return m_dropAllowed; }
    [[nodiscard]] bool dragDetached() const { return m_dragDetached; }
    [[nodiscard]] QVariantMap detachRect() const;
    [[nodiscard]] QStringList floatingSurfaceIds() const;
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] double minimumAnchorWidth() const { return m_metrics.minimumAnchorWidth; }
    [[nodiscard]] double minimumAnchorHeight() const { return m_metrics.minimumAnchorHeight; }
    [[nodiscard]] double splitterThickness() const { return m_metrics.splitterThickness; }
    void setMinimumAnchorWidth(double value);
    void setMinimumAnchorHeight(double value);
    void setSplitterThickness(double value);

    // ---- surfaces ---------------------------------------------------------
    Q_INVOKABLE QAbstractItemModel *anchors(const QString &surfaceId);
    Q_INVOKABLE QAbstractItemModel *splitters(const QString &surfaceId);
    Q_INVOKABLE void setSurfaceSize(const QString &surfaceId, double width, double height);
    // {surfaceId, primary, x, y, width, height, screen, windowMaximized,
    //  anchorCount, viewCount} in virtual desktop coordinates.
    [[nodiscard]] Q_INVOKABLE QVariantMap surface(const QString &surfaceId) const;
    // Records where the user put a floating window (debounced by the UI).
    Q_INVOKABLE bool setSurfaceGeometry(
        const QString &surfaceId,
        double x,
        double y,
        double width,
        double height,
        const QString &screenName,
        bool windowMaximized
    );
    // Opens a view or a whole panel in a new window. Returns the window id.
    Q_INVOKABLE QString detachView(const QString &viewId, double x, double y, double width, double height, const QString &screenName = {});
    Q_INVOKABLE QString detachAnchor(const QString &anchorId, double x, double y, double width, double height, const QString &screenName = {});
    // Closes a floating window; its tabs return to the main window. Nothing
    // is closed.
    Q_INVOKABLE bool closeSurface(const QString &surfaceId);
    // Re-homes floating windows whose monitor is gone (and sends them back
    // when it returns). Runs on its own when screens change; returns the
    // ids of windows that moved.
    Q_INVOKABLE QStringList recoverSurfaces();
    void setScreensForTesting(const QList<Workspace::ScreenInfo> &screens);

    // ---- Collections ------------------------------------------------------
    // Saves the current Collection's workspace and loads `collectionId`'s.
    Q_INVOKABLE void setCollection(const QString &collectionId);
    Q_INVOKABLE void saveNow();
    [[nodiscard]] Q_INVOKABLE bool hasSavedWorkspace(const QString &collectionId) const;
    // What returning to a project brings back, read from its saved
    // workspace (or the live one for the current project) without loading
    // it: { available, viewCount, titles (focused first, up to 3), layoutName }.
    // available is false when the project has no saved workspace yet.
    [[nodiscard]] Q_INVOKABLE QVariantMap savedWorkspaceSummary(const QString &collectionId) const;

    // ---- views ------------------------------------------------------------
    // resource: {kind, id, collectionId, libraryId, details}
    // options: {anchorId, index, reveal (default true), duplicate, activate,
    //           focus (default true), state, origin (default "user")}
    // With reveal, an existing view of the same type and resource is
    // activated instead of opening another one. Returns the view id.
    Q_INVOKABLE QString openView(
        const QString &typeId,
        const QVariantMap &resource,
        const QString &title,
        const QVariantMap &options = {}
    );
    // Without `force`, refuses to close the last view of a document with
    // unsaved edits and returns {ok:false, needsConfirmation:true,
    // documents:[...]}. Returns {ok, error}.
    Q_INVOKABLE QVariantMap closeView(const QString &viewId, bool force = false);
    Q_INVOKABLE QVariantMap closeAnchor(const QString &anchorId, bool force = false);
    Q_INVOKABLE QVariantMap closeOtherViews(const QString &viewId, bool force = false);
    // Documents with unsaved edits that closing these views would orphan.
    [[nodiscard]] Q_INVOKABLE QStringList unsavedDocumentsForViews(const QStringList &viewIds) const;
    Q_INVOKABLE bool activateView(const QString &viewId);
    Q_INVOKABLE bool moveView(const QString &viewId, const QString &anchorId, int index = -1);
    Q_INVOKABLE QString splitView(const QString &viewId, const QString &anchorId, const QString &edge);
    Q_INVOKABLE QString duplicateView(const QString &viewId, const QString &edge = QStringLiteral("right"));
    Q_INVOKABLE bool setViewState(const QString &viewId, const QVariantMap &state);
    Q_INVOKABLE bool mergeViewState(const QString &viewId, const QVariantMap &patch);
    Q_INVOKABLE bool setViewTitle(const QString &viewId, const QString &title);
    Q_INVOKABLE bool setViewType(const QString &viewId, const QString &typeId);
    // origin: "user" (default), "automatic" (selection following) or
    // "agent". A content-locked panel refuses the last two.
    Q_INVOKABLE bool setViewResource(const QString &viewId, const QVariantMap &resource, const QString &origin = QStringLiteral("user"));
    [[nodiscard]] Q_INVOKABLE QVariantMap view(const QString &viewId) const;
    // The view's published row: view data plus its anchor's rect.
    [[nodiscard]] Q_INVOKABLE QVariantMap viewRow(const QString &viewId) const;
    [[nodiscard]] Q_INVOKABLE QStringList viewsForResource(const QString &kind, const QString &id) const;
    [[nodiscard]] Q_INVOKABLE QStringList viewIdsOfType(const QString &typeId) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap viewType(const QString &typeId) const;

    // ---- anchors ----------------------------------------------------------
    Q_INVOKABLE QString createAnchor(const QString &anchorId = {}, const QString &edge = QStringLiteral("right"));
    Q_INVOKABLE bool focusAnchor(const QString &anchorId);
    Q_INVOKABLE bool toggleTargetAnchor(const QString &anchorId);
    Q_INVOKABLE bool toggleMaximize(const QString &anchorId);
    Q_INVOKABLE bool moveAnchor(const QString &anchorId, const QString &targetAnchorId, const QString &edge);
    Q_INVOKABLE bool mergeAnchor(const QString &anchorId, const QString &targetAnchorId);
    Q_INVOKABLE bool focusNeighbor(const QString &direction);
    Q_INVOKABLE bool cycleFocus(int step);
    Q_INVOKABLE bool cycleView(int step);
    [[nodiscard]] Q_INVOKABLE QVariantMap anchor(const QString &anchorId) const;

    // ---- anchor identity and configuration -------------------------------
    // Locks are enforced by the model; these report false (and emit
    // operationFailed with the reason) when one refuses.
    Q_INVOKABLE bool renameAnchor(const QString &anchorId, const QString &name);
    Q_INVOKABLE bool setAnchorLayoutLocked(const QString &anchorId, bool locked);
    Q_INVOKABLE bool setAnchorContentLocked(const QString &anchorId, bool locked);
    Q_INVOKABLE bool setAnchorSwitchable(const QString &anchorId, bool switchable);
    // "" clears the binding. Existing views keep their own worktree.
    Q_INVOKABLE bool setAnchorLibrary(const QString &anchorId, const QString &libraryId);
    [[nodiscard]] Q_INVOKABLE QString anchorDisplayName(const QString &anchorId) const;
    // {move, close, resize, merge, float, dockBeside, addViews, ...} plus
    // `restrictions`: {capability: reason}, for the person.
    [[nodiscard]] Q_INVOKABLE QVariantMap anchorCapabilities(const QString &anchorId) const;
    // Which worktree a new terminal (or other resource) opened in
    // `anchorId` belongs to: {libraryId, source}, source one of anchor,
    // activeView, explorer, default, none.
    [[nodiscard]] Q_INVOKABLE QVariantMap resolveLibrary(
        const QString &anchorId,
        const QString &explorerLibraryId,
        const QString &defaultLibraryId,
        const QStringList &knownLibraryIds = {}
    ) const;
    // Runs a workspace command (Core/Domains/Workspace/workspace_commands.h)
    // with the given origin ("agent" when absent). Removing a view that
    // holds the last unsaved copy of a document is refused for agents.
    Q_INVOKABLE QVariantMap runCommand(const QVariantMap &command);
    // Where a new view of `typeId` should go when nothing asks otherwise:
    // documents follow the target anchor; chats and terminals prefer an
    // anchor whose role is that type, then one already showing that type.
    [[nodiscard]] Q_INVOKABLE QString preferredAnchorFor(const QString &typeId) const;
    // The anchor whose tab strip (top `stripHeight` px, committed layout)
    // is under the point: {anchorId, localX} or {}.
    [[nodiscard]] Q_INVOKABLE QVariantMap tabStripAt(const QString &surfaceId, double x, double y, double stripHeight) const;

    // ---- splits -----------------------------------------------------------
    Q_INVOKABLE bool resizeSplit(const QString &surfaceId, const QString &nodeId, int index, double position);
    Q_INVOKABLE bool equalizeSplit(const QString &nodeId);
    Q_INVOKABLE void setResizing(bool resizing);

    // ---- drags ------------------------------------------------------------
    // kind: "view" or "anchor".
    Q_INVOKABLE bool beginDrag(const QString &kind, const QString &id);
    // Pointer in surface coordinates. A tab-strip hover passes the anchor and
    // tab index so the drop merges at that position.
    Q_INVOKABLE void updateDrag(const QString &surfaceId, double x, double y);
    Q_INVOKABLE void updateDragOverTabs(const QString &surfaceId, const QString &anchorId, int index);
    Q_INVOKABLE void clearDragTarget();
    // The pointer left every window: a drop opens a new window with this
    // frame (virtual desktop coordinates).
    Q_INVOKABLE void updateDragDetached(double x, double y, double width, double height, const QString &screenName);
    Q_INVOKABLE bool finishDrag();
    Q_INVOKABLE void cancelDrag();

    // ---- layouts ----------------------------------------------------------
    // Layouts arrange panels; they never open or close views. Presets are
    // built in; saved layouts are the user's own and shared by every
    // Collection. Applying one is a step in the workspace history.
    Q_INVOKABLE bool applyPreset(const QString &presetId);
    Q_INVOKABLE bool applyLayout(const QString &layoutId);
    Q_INVOKABLE QString saveLayout(const QString &name);
    Q_INVOKABLE bool renameLayout(const QString &layoutId, const QString &name);
    Q_INVOKABLE bool deleteLayout(const QString &layoutId);
    // Same as undo() (kept for the layout notice).
    Q_INVOKABLE bool undoLayout() { return undo(); }
    // Normalised panel rectangles for a thumbnail: [{x, y, width, height,
    // kind}] in 0..1, kind one of editor, terminal, chat, preview, diff, any.
    // "" previews the current arrangement.
    [[nodiscard]] Q_INVOKABLE QVariantList layoutPreview(const QString &layoutId) const;
    [[nodiscard]] QVariantList savedLayouts() const;
    [[nodiscard]] QString currentLayoutId() const { return m_currentLayoutId; }
    [[nodiscard]] QString currentLayoutName() const;
    [[nodiscard]] bool canUndoLayout() const { return canUndo(); }

    // ---- history ----------------------------------------------------------
    // Every arrangement change made for the person (switching layouts, an
    // agent's change, putting things back) is one step. Undo and redo walk
    // the steps through Arrangements::restore, so they only move panels and
    // tabs: views that still exist keep their state, unsaved documents and
    // terminals are never closed, and resources are never touched. Undo
    // closes the clean views a step opened; redo brings them back. Locked
    // panels refuse a step that would move them. A new step clears redo.
    // Dragging panels by hand is not recorded. Document edits have their
    // own undo.
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    [[nodiscard]] bool canUndo() const { return !m_history.isEmpty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }
    [[nodiscard]] QString undoSummary() const { return m_history.isEmpty() ? QString() : m_history.last().summary; }
    [[nodiscard]] QString redoSummary() const { return m_redo.isEmpty() ? QString() : m_redo.last().summary; }

    // ---- agents -----------------------------------------------------------
    // A workspace request from an agent tool (see workspace_intents.h and
    // AITools' workspace_* tools), run on the UI thread:
    // {action, ..., chatId, runId, explicit}. `foreground`: the conversation
    // the agent works for is the one in front.
    //
    // Applied directly only when the person asked for it in that
    // conversation (`explicit`) and it is in front, and nobody is dragging a
    // panel; otherwise checked and kept as a suggestion the person can apply
    // (agentSuggestion). Keyboard focus only moves when the request asks for
    // it and the person isn't typing. Every applied change can be undone as
    // one step, and nothing is closed except views the agent opened that
    // hold no unsaved work.
    //
    // Returns {ok, status: applied | suggested | unchanged, notes} or
    // {ok: false, code, error}.
    Q_INVOKABLE QVariantMap runAgentRequest(const QVariantMap &request, bool foreground = true);
    Q_INVOKABLE bool applyAgentSuggestion();
    Q_INVOKABLE void dismissAgentSuggestion();
    // Same as undo() (kept for the agent notice).
    Q_INVOKABLE bool undoAgentChange();
    [[nodiscard]] QVariantMap agentSuggestion() const { return m_suggestion; }
    [[nodiscard]] bool canUndoAgentChange() const { return canUndo(); }

    // Temporary arrangements. The first agent change remembers how the
    // workspace looked before it (per collection, across restarts, apart
    // from named layouts). Putting it back restores that arrangement in one
    // step; keeping it makes the current arrangement the normal one.
    [[nodiscard]] QVariantMap temporaryArrangement() const;
    Q_INVOKABLE bool putBackArrangement();
    Q_INVOKABLE void keepArrangement();

    // Preferences. Lasting ones are stored under workspace/preferences as
    // a plain JSON list; session ones are kept in memory only.
    [[nodiscard]] QVariantList workspacePreferences() const;
    // The person sets one from the UI. Returns {ok, id} or {ok: false, error}.
    Q_INVOKABLE QVariantMap setPreference(const QString &key, const QString &value, const QString &anchorId = {}, bool lasting = true);
    Q_INVOKABLE bool forgetPreference(const QString &id);
    // The purpose set for a panel ("documentation"), or "".
    [[nodiscard]] Q_INVOKABLE QString anchorPurpose(const QString &anchorId) const;
    // What agents did to the workspace this session, newest first:
    // [{at, summary, status, chatId}].
    [[nodiscard]] Q_INVOKABLE QVariantList agentActivity() const { return m_agentLog; }
    // The person typed or clicked. Also seen through an application event
    // filter, so QML rarely needs to call it.
    Q_INVOKABLE void noteUserInput();
    [[nodiscard]] bool userBusy() const;
    void setClockForTesting(std::function<qint64()> clock) { m_clock = std::move(clock); }
    Q_INVOKABLE void setCommandSurfaceState(bool collapsed, const QString &view);
    [[nodiscard]] QVariantMap commandSurface() const;
    // What agents see of the workspace (workspace_view).
    [[nodiscard]] Q_INVOKABLE QVariantMap describeForAgent() const;

signals:
    void collectionIdChanged();
    void revisionChanged();
    void dragChanged();
    void metricsChanged();
    // A view was opened, revealed or focused; the shell keeps global
    // selection (Explorer highlight, Chat composer) in step.
    void viewFocused(const QString &viewId);
    void viewOpened(const QString &viewId);
    void viewClosed(const QString &viewId, const QString &typeId, const QVariantMap &resource);
    void workspaceRestored(const QString &collectionId, bool fromSnapshot);
    void operationFailed(const QString &message);
    void layoutsChanged();
    // A layout was applied from the picker or a shortcut (not by undo).
    void layoutApplied(const QString &layoutId, const QString &name);
    void surfacesChanged();
    // A floating window was moved by monitor recovery; the window should
    // take its stored geometry.
    void surfaceRecovered(const QString &surfaceId);
    void agentChanged();
    void historyChanged();
    // An agent changed the layout; `summary` says what, in plain words.
    void agentArranged(const QString &summary);
    // An agent put an earlier arrangement back (nothing to undo).
    void agentRestored(const QString &summary);
    void preferencesChanged();
    // Asks the shell to open (or close) the bottom workbench on `view`
    // ("chat" or "terminal").
    void commandSurfaceRequested(const QString &view, bool open);
    void commandSurfaceChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void publish();
    void publishSurface(const QString &surfaceId);
    void scheduleSave();
    void retainResources(const QString &viewId);
    void releaseResources(const QString &viewId, const QString &typeId, const Workspace::ResourceRef &resource);
    void retainAll();
    void releaseAll();
    bool check(const Workspace::OpResult &result);
    [[nodiscard]] QString snapshotKey(const QString &collectionId) const;
    [[nodiscard]] QSizeF surfaceSize(const QString &surfaceId) const;
    [[nodiscard]] static Workspace::ResourceRef resourceFromMap(const QVariantMap &map);
    [[nodiscard]] static QVariantMap resourceToMap(const Workspace::ResourceRef &resource);
    [[nodiscard]] static bool parseEdgeName(const QString &name, Workspace::Edge *edge);
    void setDropTarget(const Workspace::DropTarget &target);
    [[nodiscard]] QList<Workspace::ScreenInfo> currentScreens() const;
    [[nodiscard]] bool canDetach(const Workspace::DragSource &source) const;
    void resetDrag();

    Workspace::ViewTypeRegistry m_registry;
    Workspace::WorkspaceModel m_model;
    Workspace::LayoutMetrics m_metrics;
    // The last arrangement an agent composed, for follow-ups ("give the
    // images more room") that adjust it instead of starting over.
    QVariantMap m_lastComposition;
    QString m_collectionId;
    int m_revision = 0;
    int m_publishedModelRevision = -1;
    KeyedListModel *m_views = nullptr;
    QHash<QString, KeyedListModel *> m_anchorModels;
    QHash<QString, KeyedListModel *> m_splitterModels;
    QHash<QString, QSizeF> m_surfaceSizes;
    QSizeF m_primarySize;
    QPointer<DocumentStore> m_documents;
    QHash<QString, QString> m_retained; // editor view -> document it holds
    QSettings m_settings;
    QTimer m_saveTimer;
    bool m_resizing = false;

    Workspace::DragSource m_drag;
    Workspace::DropTarget m_dropTarget;
    bool m_dropAllowed = false;
    bool m_dragDetached = false;
    QRectF m_detachRect;
    QString m_detachScreen;
    QStringList m_publishedSurfaces;
    QList<Workspace::ScreenInfo> m_testScreens;
    bool m_useTestScreens = false;
    QRectF m_landingRect;
    Workspace::LayoutGeometry m_preview;
    QString m_previewSurfaceId;
    bool applyLayoutDocument(const QJsonObject &layout, const QString &layoutId);
    [[nodiscard]] QJsonObject layoutDocument(const QString &layoutId) const;
    [[nodiscard]] QJsonArray readSavedLayouts() const;
    [[nodiscard]] QString layoutName(const QString &layoutId) const;
    void writeSavedLayouts(const QJsonArray &layouts);
    void setCurrentLayoutId(const QString &layoutId);

    // ---- agents (workspace_store_agent.cpp) ------------------------------
    // One history step: the state to go back to (on the undo list) or
    // forward to (on the redo list).
    struct HistoryStep
    {
        Workspace::WorkspaceModel::Data state;
        QStringList close;  // views this step opened: closed when going back (if clean)
        QStringList reopen; // views this step closed: brought back when going back
        QString summary;
        bool surfaceTouched = false; // the bottom panel changed too
        bool surfaceCollapsed = false;
        QString surfaceView;
        QString layoutId;        // current layout at `state`
        QJsonObject arrangement; // the temporary arrangement record at `state`
    };
    // A step whose state is now (before a change).
    [[nodiscard]] HistoryStep historyStep(const QString &summary) const;
    void recordStep(HistoryStep step);
    // Goes to `step`'s state; returns the step that comes back here.
    bool restoreStep(const HistoryStep &step, HistoryStep *inverse, QString *code, QString *error);
    bool undoSteps(qsizetype count, QString *code, QString *error);
    bool redoStep(QString *code, QString *error);
    [[nodiscard]] QJsonObject arrangementRecord() const;
    void setArrangementRecord(const QJsonObject &record);
    [[nodiscard]] Workspace::Intents::Environment agentEnvironment(bool mayMoveFocus) const;
    [[nodiscard]] QVariantMap agentIntent(const QVariantMap &request, QVariantMap *failure) const;
    [[nodiscard]] QVariantMap composeIntent(const QVariantMap &request, QVariantMap *failure) const;
    [[nodiscard]] QVariantMap patchIntent(const Workspace::Compose::Spec &spec, QVariantMap *failure) const;
    // Compact: each panel's name, position and what it shows.
    [[nodiscard]] QVariantList panelsForAgent() const;
    [[nodiscard]] QStringList openViewsNamed(const QString &reference, const QString &libraryId) const;
    [[nodiscard]] QString verifyComposition(const Workspace::WorkspaceModel &model, const QVariantMap &intent, qsizetype viewsBefore, bool focusMoved) const;
    QVariantMap applyAgentIntent(const QVariantMap &intent, const QString &chatId, bool mayMoveFocus);
    QVariantMap suggestAgentIntent(const QVariantMap &intent, const QString &chatId, const QString &reason);
    QVariantMap restoreForAgent(const QVariantMap &request);
    bool restoreTo(const Workspace::WorkspaceModel::Data &arrangement, const QStringList &opened, QString *code, QString *error,
                   const QStringList &reopen = {}, QStringList *closed = nullptr, QStringList *reopened = nullptr);
    [[nodiscard]] QSet<QString> closableAgentViews(const QStringList &opened) const;
    void logAgent(const QString &summary, const QString &status, const QString &chatId);
    void noteFocusHistory();
    [[nodiscard]] qint64 now() const;
    [[nodiscard]] QString arrangementKey(const QString &collectionId) const;
    void rememberArrangement(const Workspace::WorkspaceModel::Data &before, const QStringList &opened, const QString &summary);
    void loadArrangement();
    void forgetArrangement();
    bool restoreArrangement(QString *code, QString *error);
    QVariantMap preferenceForAgent(const QVariantMap &request);
    QVariantMap layoutForAgent(const QVariantMap &request);
    [[nodiscard]] QString layoutIdNamed(const QString &name) const;
    [[nodiscard]] QVariantList layoutsForAgent() const;
    QVariantMap addPreference(Workspace::Preferences::Preference preference);
    void loadPreferences();
    void savePreferences();
    [[nodiscard]] Workspace::Preferences::Effects preferenceEffects() const;

    QList<HistoryStep> m_history;
    QList<HistoryStep> m_redo;
    QVariantMap m_suggestion;
    QVariantMap m_suggestionIntent;
    QVariantList m_agentLog;
    qint64 m_lastUserInput = 0;
    std::function<qint64()> m_clock;
    bool m_commandSurfaceCollapsed = false;
    QString m_commandSurfaceView = QStringLiteral("chat");
    QStringList m_recentAnchors;
    // The arrangement before the agent's task, as a snapshot without focus,
    // and every view agents opened since (candidates to close on put back).
    QJsonObject m_baseline;
    QStringList m_baselineOpened;
    QStringList m_baselineChanges;
    QString m_baselineSince;
    QList<Workspace::Preferences::Preference> m_preferences;

    QString m_currentLayoutId;
    QHash<QString, QRectF> m_lastViewRects;
    QHash<QString, QRectF> m_lastAnchorRects;
};
