#pragma once

#include "App/Domains/Workspace/keyed_list_model.h"
#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_model.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

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
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QVariantList savedLayouts READ savedLayouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString currentLayoutId READ currentLayoutId NOTIFY layoutsChanged)
    Q_PROPERTY(QString currentLayoutName READ currentLayoutName NOTIFY layoutsChanged)
    Q_PROPERTY(bool canUndoLayout READ canUndoLayout NOTIFY layoutsChanged)
    Q_PROPERTY(double minimumAnchorWidth READ minimumAnchorWidth WRITE setMinimumAnchorWidth NOTIFY metricsChanged)
    Q_PROPERTY(double minimumAnchorHeight READ minimumAnchorHeight WRITE setMinimumAnchorHeight NOTIFY metricsChanged)
    Q_PROPERTY(double splitterThickness READ splitterThickness WRITE setSplitterThickness NOTIFY metricsChanged)

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

    // ---- Collections ------------------------------------------------------
    // Saves the current Collection's workspace and loads `collectionId`'s.
    Q_INVOKABLE void setCollection(const QString &collectionId);
    Q_INVOKABLE void saveNow();
    [[nodiscard]] Q_INVOKABLE bool hasSavedWorkspace(const QString &collectionId) const;

    // ---- views ------------------------------------------------------------
    // resource: {kind, id, collectionId, libraryId, details}
    // options: {anchorId, index, reveal (default true), duplicate, activate,
    //           focus (default true), state}
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
    Q_INVOKABLE bool setViewResource(const QString &viewId, const QVariantMap &resource);
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
    Q_INVOKABLE bool finishDrag();
    Q_INVOKABLE void cancelDrag();

    // ---- layouts ----------------------------------------------------------
    // Layouts arrange panels; they never open or close views. Presets are
    // built in; saved layouts are the user's own and shared by every
    // Collection. Applying one can be undone (the arrangement before it is
    // kept until the next layout change).
    Q_INVOKABLE bool applyPreset(const QString &presetId);
    Q_INVOKABLE bool applyLayout(const QString &layoutId);
    Q_INVOKABLE QString saveLayout(const QString &name);
    Q_INVOKABLE bool renameLayout(const QString &layoutId, const QString &name);
    Q_INVOKABLE bool deleteLayout(const QString &layoutId);
    Q_INVOKABLE bool undoLayout();
    // Normalised panel rectangles for a thumbnail: [{x, y, width, height,
    // kind}] in 0..1, kind one of editor, terminal, chat, preview, diff, any.
    // "" previews the current arrangement.
    [[nodiscard]] Q_INVOKABLE QVariantList layoutPreview(const QString &layoutId) const;
    [[nodiscard]] QVariantList savedLayouts() const;
    [[nodiscard]] QString currentLayoutId() const { return m_currentLayoutId; }
    [[nodiscard]] QString currentLayoutName() const;
    [[nodiscard]] bool canUndoLayout() const { return !m_layoutUndo.isEmpty(); }

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

    Workspace::ViewTypeRegistry m_registry;
    Workspace::WorkspaceModel m_model;
    Workspace::LayoutMetrics m_metrics;
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
    QRectF m_landingRect;
    Workspace::LayoutGeometry m_preview;
    QString m_previewSurfaceId;
    bool applyLayoutDocument(const QJsonObject &layout, const QString &layoutId);
    [[nodiscard]] QJsonObject layoutDocument(const QString &layoutId) const;
    [[nodiscard]] QJsonArray readSavedLayouts() const;
    void writeSavedLayouts(const QJsonArray &layouts);
    void setCurrentLayoutId(const QString &layoutId);

    QString m_currentLayoutId;
    QJsonObject m_layoutUndo;
    QString m_layoutUndoId;
    QHash<QString, QRectF> m_lastViewRects;
    QHash<QString, QRectF> m_lastAnchorRects;
};
