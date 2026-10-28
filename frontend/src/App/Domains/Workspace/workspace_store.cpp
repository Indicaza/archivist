#include "App/Domains/Workspace/workspace_store.h"

#include "App/Domains/Documents/document_store.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_commands.h"
#include "Core/Domains/Workspace/workspace_placement.h"
#include "Core/Domains/Workspace/workspace_presets.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QScreen>
#include <QUuid>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

using namespace Workspace;

namespace
{
// Role names avoid Item's own properties (x, state, focus, ...) so QML
// delegates can take them as required properties.
const QStringList kAnchorRoles = {
    QStringLiteral("anchorId"),
    QStringLiteral("surfaceId"),
    QStringLiteral("rectX"),
    QStringLiteral("rectY"),
    QStringLiteral("rectWidth"),
    QStringLiteral("rectHeight"),
    QStringLiteral("viewIds"),
    QStringLiteral("activeViewId"),
    QStringLiteral("activeTypeId"),
    QStringLiteral("pinned"),
    QStringLiteral("roles"),
    QStringLiteral("isFocused"),
    QStringLiteral("isTarget"),
    QStringLiteral("isMaximized"),
    QStringLiteral("isHidden"),
    QStringLiteral("isDragSource"),
    QStringLiteral("isVacating"),
    QStringLiteral("name"),
    QStringLiteral("displayName"),
    QStringLiteral("layoutLocked"),
    QStringLiteral("contentLocked"),
    QStringLiteral("switchable"),
    QStringLiteral("contextLibraryId"),
};

const QStringList kViewRoles = {
    QStringLiteral("viewId"),
    QStringLiteral("typeId"),
    QStringLiteral("title"),
    QStringLiteral("resourceKind"),
    QStringLiteral("resourceId"),
    QStringLiteral("collectionId"),
    QStringLiteral("libraryId"),
    QStringLiteral("details"),
    QStringLiteral("viewState"),
    QStringLiteral("anchorId"),
    QStringLiteral("surfaceId"),
    QStringLiteral("tabIndex"),
    QStringLiteral("isActive"),
    QStringLiteral("isFocused"),
    QStringLiteral("rectX"),
    QStringLiteral("rectY"),
    QStringLiteral("rectWidth"),
    QStringLiteral("rectHeight"),
    QStringLiteral("isHidden"),
};

const QStringList kSplitterRoles = {
    QStringLiteral("key"),
    QStringLiteral("nodeId"),
    QStringLiteral("splitIndex"),
    QStringLiteral("orientation"),
    QStringLiteral("locked"),
    QStringLiteral("rectX"),
    QStringLiteral("rectY"),
    QStringLiteral("rectWidth"),
    QStringLiteral("rectHeight"),
};

QVariantMap rectMap(const QRectF &rect)
{
    return {
        {QStringLiteral("rectX"), rect.x()},
        {QStringLiteral("rectY"), rect.y()},
        {QStringLiteral("rectWidth"), rect.width()},
        {QStringLiteral("rectHeight"), rect.height()},
    };
}

// Views that hold a document buffer: editors, and rendered previews of text
// files (so a Markdown preview shows unsaved edits live).
bool isDocumentView(const ViewRecord &view)
{
    if (view.resource.kind != QStringLiteral("document") || view.resource.id.isEmpty()) {
        return false;
    }
    return view.typeId == ViewTypes::Editor
        || (view.typeId == ViewTypes::Preview && view.resource.details.value(QStringLiteral("textual")).toBool());
}
} // namespace

WorkspaceStore::WorkspaceStore(QObject *parent)
    : QObject(parent)
    , m_registry(ViewTypeRegistry::withBuiltIns())
    , m_views(new KeyedListModel(QStringLiteral("viewId"), kViewRoles, this))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &WorkspaceStore::saveNow);

    // Monitors come and go (a laptop leaves its dock): bring floating
    // windows back onto a screen that exists.
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        auto recover = [this]() {
            QTimer::singleShot(250, this, [this]() { recoverSurfaces(); });
        };
        connect(app, &QGuiApplication::screenAdded, this, recover);
        connect(app, &QGuiApplication::screenRemoved, this, recover);
        connect(app, &QGuiApplication::primaryScreenChanged, this, recover);
    }

    // Typing and clicking anywhere tell agents to keep their hands off focus.
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }

    loadPreferences();
    publish();
}

WorkspaceStore::~WorkspaceStore()
{
    if (m_saveTimer.isActive()) {
        saveNow();
    }
}

void WorkspaceStore::setDocumentStore(DocumentStore *documents)
{
    m_documents = documents;
    retainAll();
}

// ---- properties ----------------------------------------------------------

QString WorkspaceStore::focusedViewId() const
{
    const AnchorRecord *anchor = m_model.anchor(m_model.focusedAnchorId());
    return anchor ? anchor->activeViewId : QString();
}

QString WorkspaceStore::dragKind() const
{
    switch (m_drag.kind) {
    case DragSource::Kind::View:
        return QStringLiteral("view");
    case DragSource::Kind::Anchor:
        return QStringLiteral("anchor");
    case DragSource::Kind::None:
        break;
    }
    return {};
}

QVariantMap WorkspaceStore::dropTarget() const
{
    if (!m_dropTarget.isValid()) {
        return {};
    }

    return {
        {QStringLiteral("surfaceId"), m_dropTarget.surfaceId},
        {QStringLiteral("anchorId"), m_dropTarget.anchorId},
        {QStringLiteral("edge"), edgeName(m_dropTarget.edge)},
        {QStringLiteral("index"), m_dropTarget.index},
        {QStringLiteral("surfaceEdge"), m_dropTarget.isSurfaceEdge()},
    };
}

QVariantMap WorkspaceStore::dragLandingRect() const
{
    if (!m_dropAllowed || m_landingRect.isEmpty()) {
        return {};
    }

    return {
        {QStringLiteral("x"), m_landingRect.x()},
        {QStringLiteral("y"), m_landingRect.y()},
        {QStringLiteral("width"), m_landingRect.width()},
        {QStringLiteral("height"), m_landingRect.height()},
        {QStringLiteral("surfaceId"), m_dropTarget.surfaceId},
    };
}

QVariantList WorkspaceStore::presets() const
{
    QVariantList list;

    for (const Presets::Preset &preset : Presets::all()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("name"), preset.name},
            {QStringLiteral("description"), preset.description},
        });
    }

    return list;
}

void WorkspaceStore::setMinimumAnchorWidth(double value)
{
    if (qFuzzyCompare(m_metrics.minimumAnchorWidth, value) || value <= 0) {
        return;
    }
    m_metrics.minimumAnchorWidth = value;
    emit metricsChanged();
    publish();
}

void WorkspaceStore::setMinimumAnchorHeight(double value)
{
    if (qFuzzyCompare(m_metrics.minimumAnchorHeight, value) || value <= 0) {
        return;
    }
    m_metrics.minimumAnchorHeight = value;
    emit metricsChanged();
    publish();
}

void WorkspaceStore::setSplitterThickness(double value)
{
    if (qFuzzyCompare(m_metrics.splitterThickness, value) || value < 0) {
        return;
    }
    m_metrics.splitterThickness = value;
    emit metricsChanged();
    publish();
}

// ---- surfaces ------------------------------------------------------------

QAbstractItemModel *WorkspaceStore::anchors(const QString &surfaceId)
{
    if (!m_anchorModels.contains(surfaceId)) {
        m_anchorModels.insert(surfaceId, new KeyedListModel(QStringLiteral("anchorId"), kAnchorRoles, this));
        publishSurface(surfaceId);
    }
    return m_anchorModels.value(surfaceId);
}

QAbstractItemModel *WorkspaceStore::splitters(const QString &surfaceId)
{
    if (!m_splitterModels.contains(surfaceId)) {
        m_splitterModels.insert(surfaceId, new KeyedListModel(QStringLiteral("key"), kSplitterRoles, this));
        publishSurface(surfaceId);
    }
    return m_splitterModels.value(surfaceId);
}

void WorkspaceStore::setSurfaceSize(const QString &surfaceId, double width, double height)
{
    const QSizeF size(qMax(0.0, width), qMax(0.0, height));

    if (m_surfaceSizes.value(surfaceId) == size) {
        return;
    }

    m_surfaceSizes.insert(surfaceId, size);
    if (surfaceId == m_model.primarySurfaceId()) {
        m_primarySize = size;
    }

    // A resize mid-drag invalidates the preview; recompute it.
    if (m_dropTarget.isValid() && m_dropTarget.surfaceId == surfaceId) {
        const DropTarget target = m_dropTarget;
        m_dropTarget = {};
        setDropTarget(target);
        return;
    }

    publish();
}

QSizeF WorkspaceStore::surfaceSize(const QString &surfaceId) const
{
    QSizeF size = m_surfaceSizes.value(surfaceId);

    // Applying a layout or loading a snapshot gives the main window a new
    // surface id; it is still the same window, with the same size.
    if (size.isEmpty() && surfaceId == m_model.primarySurfaceId()) {
        size = m_primarySize;
    }

    return size.isEmpty() ? QSizeF(1200, 800) : size;
}

// ---- publishing ----------------------------------------------------------

void WorkspaceStore::publish()
{
    QList<QVariantMap> viewRows;
    const QString focusedAnchor = m_model.focusedAnchorId();
    const QString target = m_model.targetAnchorId();
    QHash<QString, LayoutGeometry> geometries;
    QHash<QString, QRectF> lastRects;

    for (const QString &surfaceId : m_model.surfaceIds()) {
        geometries.insert(
            surfaceId,
            m_previewSurfaceId == surfaceId ? m_preview : m_model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics)
        );
    }

    for (const QString &surfaceId : m_model.surfaceIds()) {
        const LayoutGeometry &geometry = geometries[surfaceId];

        for (const QString &anchorId : m_model.anchorIds(surfaceId)) {
            const AnchorRecord *anchor = m_model.anchor(anchorId);
            const bool hidden = !geometry.anchors.contains(anchorId);
            const QRectF rect = geometry.anchors.value(anchorId);

            for (int index = 0; index < anchor->views.size(); ++index) {
                const ViewRecord *view = m_model.view(anchor->views.at(index));

                if (!view) {
                    continue;
                }

                // A hidden view keeps its last place, so it never flies in
                // from a corner (and never squeezes a terminal to nothing).
                if (!hidden) {
                    lastRects.insert(view->id, rect);
                }
                QVariantMap row = rectMap(hidden ? m_lastViewRects.value(view->id, rect) : rect);
                if (hidden && m_lastViewRects.contains(view->id)) {
                    lastRects.insert(view->id, m_lastViewRects.value(view->id));
                }
                row.insert(QStringLiteral("viewId"), view->id);
                row.insert(QStringLiteral("typeId"), view->typeId);
                row.insert(QStringLiteral("title"), view->title);
                row.insert(QStringLiteral("resourceKind"), view->resource.kind);
                row.insert(QStringLiteral("resourceId"), view->resource.id);
                row.insert(QStringLiteral("collectionId"), view->resource.collectionId);
                row.insert(QStringLiteral("libraryId"), view->resource.libraryId);
                row.insert(QStringLiteral("details"), view->resource.details);
                row.insert(QStringLiteral("viewState"), view->state);
                row.insert(QStringLiteral("anchorId"), anchorId);
                row.insert(QStringLiteral("surfaceId"), surfaceId);
                row.insert(QStringLiteral("tabIndex"), index);
                row.insert(QStringLiteral("isActive"), anchor->activeViewId == view->id);
                row.insert(QStringLiteral("isFocused"), anchor->activeViewId == view->id && focusedAnchor == anchorId);
                row.insert(QStringLiteral("isHidden"), hidden);
                viewRows.append(row);
            }
        }
    }

    m_views->setRows(viewRows);
    m_lastViewRects = lastRects;

    for (auto it = m_anchorModels.cbegin(); it != m_anchorModels.cend(); ++it) {
        publishSurface(it.key());
    }
    for (auto it = m_splitterModels.cbegin(); it != m_splitterModels.cend(); ++it) {
        if (!m_anchorModels.contains(it.key())) {
            publishSurface(it.key());
        }
    }

    // Keep document buffers in step with the editor views that show them.
    QHash<QString, QString> wanted; // view -> document

    for (const QString &viewId : m_model.viewIds()) {
        const ViewRecord *view = m_model.view(viewId);
        if (view && isDocumentView(*view)) {
            wanted.insert(viewId, view->resource.id);
        }
    }

    if (m_documents) {
        for (auto it = m_retained.cbegin(); it != m_retained.cend(); ++it) {
            if (wanted.value(it.key()) != it.value()) {
                m_documents->release(it.value(), it.key());
            }
        }
        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
            if (m_retained.value(it.key()) != it.value()) {
                retainResources(it.key());
            }
        }
        m_retained = wanted;
    }

    noteFocusHistory();

    const bool modelChanged = m_publishedModelRevision != m_model.revision();
    m_publishedModelRevision = m_model.revision();
    ++m_revision;
    emit revisionChanged();

    const QStringList floating = floatingSurfaceIds();
    if (floating != m_publishedSurfaces) {
        m_publishedSurfaces = floating;
        emit surfacesChanged();
    }

    if (modelChanged) {
        scheduleSave();
    }
}

void WorkspaceStore::publishSurface(const QString &surfaceId)
{
    const LayoutGeometry geometry = m_previewSurfaceId == surfaceId
        ? m_preview
        : m_model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics);
    const SurfaceRecord *surface = m_model.surface(surfaceId);
    const QString focusedAnchor = m_model.focusedAnchorId();
    const QString target = m_model.explicitTargetAnchorId();

    if (KeyedListModel *anchors = m_anchorModels.value(surfaceId)) {
        QList<QVariantMap> rows;

        // The Anchor the pointer is dragging from must stay alive while the
        // drag lasts, even when the preview folds it away: it holds the
        // pointer grab. It keeps its old place and fades out instead.
        QString holder;
        if (m_drag.kind == DragSource::Kind::Anchor) {
            holder = m_drag.id;
        } else if (m_drag.kind == DragSource::Kind::View) {
            holder = m_model.anchorOfView(m_drag.id);
        }
        LayoutGeometry committed;
        const bool previewing = m_previewSurfaceId == surfaceId;
        if (previewing && !holder.isEmpty() && !geometry.anchors.contains(holder)) {
            committed = m_model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics);
        }

        if (surface) {
            for (const QString &anchorId : m_model.anchorIds(surfaceId)) {
                const AnchorRecord *anchor = m_model.anchor(anchorId);
                const ViewRecord *active = m_model.view(anchor->activeViewId);
                const bool vacating = previewing && anchorId == holder
                    && !geometry.anchors.contains(anchorId) && committed.anchors.contains(anchorId);
                const bool shown = vacating || geometry.anchors.contains(anchorId);
                const QRectF rect = vacating ? committed.anchors.value(anchorId)
                    : shown ? geometry.anchors.value(anchorId)
                    : m_lastAnchorRects.value(anchorId);
                if (shown) {
                    m_lastAnchorRects.insert(anchorId, rect);
                }
                QVariantMap row = rectMap(rect);
                row.insert(QStringLiteral("anchorId"), anchorId);
                row.insert(QStringLiteral("surfaceId"), surfaceId);
                row.insert(QStringLiteral("viewIds"), anchor->views);
                row.insert(QStringLiteral("activeViewId"), anchor->activeViewId);
                row.insert(QStringLiteral("activeTypeId"), active ? active->typeId : QString());
                row.insert(QStringLiteral("pinned"), anchor->pinned);
                row.insert(QStringLiteral("roles"), anchor->roles);
                row.insert(QStringLiteral("isFocused"), anchorId == focusedAnchor);
                row.insert(QStringLiteral("isTarget"), anchorId == target);
                row.insert(QStringLiteral("isMaximized"), surface->maximizedAnchorId == anchorId);
                row.insert(QStringLiteral("isHidden"), !vacating && !geometry.anchors.contains(anchorId));
                row.insert(QStringLiteral("isVacating"), vacating);
                row.insert(QStringLiteral("name"), anchor->name);
                row.insert(QStringLiteral("displayName"), m_model.anchorDisplayName(anchorId));
                row.insert(QStringLiteral("layoutLocked"), anchor->layoutLocked);
                row.insert(QStringLiteral("contentLocked"), anchor->contentLocked);
                row.insert(QStringLiteral("switchable"), anchor->switchable);
                row.insert(QStringLiteral("contextLibraryId"), anchor->libraryId);
                row.insert(
                    QStringLiteral("isDragSource"),
                    m_drag.kind == DragSource::Kind::Anchor && m_drag.id == anchorId
                );
                rows.append(row);
            }
        }

        anchors->setRows(rows);
    }

    if (KeyedListModel *splitters = m_splitterModels.value(surfaceId)) {
        QList<QVariantMap> rows;

        // Splitters follow the committed layout, not a drag preview.
        const LayoutGeometry committed = m_previewSurfaceId == surfaceId
            ? m_model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics)
            : geometry;

        if (surface && m_previewSurfaceId != surfaceId) {
            for (const SplitterHandle &handle : committed.splitters) {
                QVariantMap row = rectMap(handle.rect);
                row.insert(QStringLiteral("key"), handle.nodeId + QLatin1Char(':') + QString::number(handle.index));
                row.insert(QStringLiteral("nodeId"), handle.nodeId);
                row.insert(QStringLiteral("splitIndex"), handle.index);
                row.insert(QStringLiteral("orientation"), orientationName(handle.orientation));
                row.insert(QStringLiteral("locked"), m_model.isSplitBoundaryLocked(handle.nodeId, handle.index));
                rows.append(row);
            }
        }

        splitters->setRows(rows);
    }
}

bool WorkspaceStore::check(const OpResult &result)
{
    if (!result.ok) {
        emit operationFailed(result.message.isEmpty() ? result.code : result.message);
        return false;
    }

    publish();
    return true;
}

// ---- persistence ---------------------------------------------------------

QString WorkspaceStore::snapshotKey(const QString &collectionId) const
{
    return QStringLiteral("workspace/collections/%1/docking/snapshot").arg(collectionId);
}

void WorkspaceStore::scheduleSave()
{
    if (!m_collectionId.isEmpty() && !m_resizing) {
        m_saveTimer.start();
    }
}

void WorkspaceStore::saveNow()
{
    m_saveTimer.stop();

    if (m_collectionId.isEmpty()) {
        return;
    }

    const QJsonObject snapshot = Codec::encodeSnapshot(m_model);
    m_settings.setValue(snapshotKey(m_collectionId), QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));
}

bool WorkspaceStore::hasSavedWorkspace(const QString &collectionId) const
{
    return !collectionId.isEmpty() && m_settings.contains(snapshotKey(collectionId));
}

void WorkspaceStore::setCollection(const QString &collectionId)
{
    if (collectionId == m_collectionId) {
        return;
    }

    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }

    saveNow();
    releaseAll();

    m_collectionId = collectionId;
    WorkspaceModel fresh;
    bool fromSnapshot = false;

    if (!collectionId.isEmpty()) {
        const QString text = m_settings.value(snapshotKey(collectionId)).toString();
        const QJsonObject snapshot = QJsonDocument::fromJson(text.toUtf8()).object();

        if (!snapshot.isEmpty()) {
            const Codec::LoadReport report = Codec::decodeSnapshot(snapshot, m_registry, fresh);
            fromSnapshot = report.ok;

            if (!report.ok) {
                // Keep the unreadable snapshot instead of overwriting it.
                m_settings.setValue(snapshotKey(collectionId) + QStringLiteral(".unreadable"), text);
                emit operationFailed(report.error);
            }
        }
    }

    m_model = fresh;
    m_publishedModelRevision = m_model.revision();
    m_agentUndo.clear();
    loadArrangement();
    m_suggestion.clear();
    m_suggestionIntent.clear();
    m_recentAnchors.clear();
    emit agentChanged();
    m_layoutUndo = {};
    m_layoutUndoId.clear();
    m_currentLayoutId = collectionId.isEmpty()
        ? QString()
        : m_settings.value(QStringLiteral("workspace/collections/%1/docking/layoutId").arg(collectionId)).toString();
    emit layoutsChanged();
    emit collectionIdChanged();
    m_model.recoverSurfaces(currentScreens());
    publish();
    emit workspaceRestored(collectionId, fromSnapshot);
}

// ---- resources -----------------------------------------------------------

void WorkspaceStore::retainResources(const QString &viewId)
{
    const ViewRecord *view = m_model.view(viewId);

    if (!m_documents || !view || !isDocumentView(*view)) {
        return;
    }

    QVariantMap seed = view->resource.details;
    seed.insert(QStringLiteral("collectionId"), view->resource.collectionId);
    seed.insert(QStringLiteral("libraryId"), view->resource.libraryId);
    if (!seed.contains(QStringLiteral("fileId"))) {
        seed.insert(QStringLiteral("fileId"), view->resource.id.section(QLatin1Char(':'), 2));
    }
    m_documents->retain(view->resource.id, viewId, seed);
}

void WorkspaceStore::releaseResources(const QString &viewId, const QString &typeId, const ResourceRef &resource)
{
    Q_UNUSED(typeId)
    Q_UNUSED(resource)

    if (m_documents && m_retained.contains(viewId)) {
        m_documents->release(m_retained.take(viewId), viewId);
    }
}

void WorkspaceStore::retainAll()
{
    m_retained.clear();
    publish();
}

void WorkspaceStore::releaseAll()
{
    if (m_documents) {
        for (auto it = m_retained.cbegin(); it != m_retained.cend(); ++it) {
            m_documents->release(it.value(), it.key());
        }
    }
    m_retained.clear();
}

// ---- views ---------------------------------------------------------------

ResourceRef WorkspaceStore::resourceFromMap(const QVariantMap &map)
{
    ResourceRef resource;
    resource.kind = map.value(QStringLiteral("kind")).toString();
    resource.id = map.value(QStringLiteral("id")).toString();
    resource.collectionId = map.value(QStringLiteral("collectionId")).toString();
    resource.libraryId = map.value(QStringLiteral("libraryId")).toString();
    resource.details = map.value(QStringLiteral("details")).toMap();
    return resource;
}

QVariantMap WorkspaceStore::resourceToMap(const ResourceRef &resource)
{
    return {
        {QStringLiteral("kind"), resource.kind},
        {QStringLiteral("id"), resource.id},
        {QStringLiteral("collectionId"), resource.collectionId},
        {QStringLiteral("libraryId"), resource.libraryId},
        {QStringLiteral("details"), resource.details},
    };
}

bool WorkspaceStore::parseEdgeName(const QString &name, Edge *edge)
{
    return parseEdge(name.toLower(), edge);
}

QString WorkspaceStore::openView(const QString &typeId, const QVariantMap &resourceMap, const QString &title, const QVariantMap &options)
{
    const ResourceRef resource = resourceFromMap(resourceMap);
    const bool reveal = options.value(QStringLiteral("reveal"), true).toBool()
        && !options.value(QStringLiteral("duplicate")).toBool();
    const bool focus = options.value(QStringLiteral("focus"), true).toBool();
    Origin origin = Origin::User;
    // An unknown origin counts as the person.
    static_cast<void>(parseOrigin(options.value(QStringLiteral("origin")).toString(), &origin));
    QString anchorId = options.value(QStringLiteral("anchorId")).toString();
    if (anchorId.isEmpty() || !m_model.anchor(anchorId)) {
        anchorId = preferredAnchorFor(typeId);
    }

    if (reveal && !resource.isEmpty()) {
        // Prefer a view in the requested (or target) anchor, then any.
        const QString existing = Placement::existingView(m_model, typeId, resource, anchorId);

        if (!existing.isEmpty()) {
            m_model.activateView(existing);
            if (focus) {
                m_model.focusAnchor(m_model.anchorOfView(existing));
            }
            if (!title.isEmpty()) {
                m_model.setViewTitle(existing, title);
            }
            publish();
            emit viewFocused(existing);
            return existing;
        }
    }

    ViewRecord view;
    view.typeId = typeId;
    view.resource = resource;
    view.title = title;
    view.state = options.value(QStringLiteral("state")).toMap();
    if (const ViewTypeDescriptor *type = m_registry.find(typeId)) {
        view.stateVersion = type->stateVersion;
    }

    const OpResult result = m_model.openView(
        view,
        anchorId,
        options.value(QStringLiteral("index"), -1).toInt(),
        options.value(QStringLiteral("activate"), true).toBool(),
        origin
    );

    if (!result.ok) {
        emit operationFailed(result.message);
        return {};
    }

    if (focus) {
        m_model.focusAnchor(m_model.anchorOfView(result.createdId));
    }

    publish();
    emit viewOpened(result.createdId);
    if (focus) {
        emit viewFocused(result.createdId);
    }
    return result.createdId;
}

QStringList WorkspaceStore::unsavedDocumentsForViews(const QStringList &viewIds) const
{
    QStringList documents;

    if (!m_documents) {
        return documents;
    }

    const QSet<QString> closing(viewIds.cbegin(), viewIds.cend());

    for (const QString &viewId : viewIds) {
        const QString documentId = m_retained.value(viewId);
        const ViewRecord *closingView = m_model.view(viewId);

        if (!closingView || closingView->typeId != ViewTypes::Editor) {
            continue;
        }

        if (documentId.isEmpty() || documents.contains(documentId) || !m_documents->isDirty(documentId)) {
            continue;
        }

        bool shownElsewhere = false;

        // Only another editor counts: a rendered preview does not show that
        // the text is unsaved.
        for (auto it = m_retained.cbegin(); it != m_retained.cend(); ++it) {
            const ViewRecord *other = m_model.view(it.key());
            if (it.value() == documentId && !closing.contains(it.key()) && other
                && other->typeId == ViewTypes::Editor) {
                shownElsewhere = true;
                break;
            }
        }

        if (!shownElsewhere) {
            documents.append(documentId);
        }
    }

    return documents;
}

namespace
{
QVariantMap closeResult(bool ok, const QString &error = {}, const QStringList &documents = {})
{
    QVariantMap result{{QStringLiteral("ok"), ok}};
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
    }
    if (!documents.isEmpty()) {
        result.insert(QStringLiteral("needsConfirmation"), true);
        result.insert(QStringLiteral("documents"), documents);
    }
    return result;
}
} // namespace

QVariantMap WorkspaceStore::closeView(const QString &viewId, bool force)
{
    const ViewRecord *record = m_model.view(viewId);

    if (!record) {
        return closeResult(false, QStringLiteral("Unknown view."));
    }

    if (!force) {
        const QStringList unsaved = unsavedDocumentsForViews({viewId});
        if (!unsaved.isEmpty()) {
            return closeResult(false, {}, unsaved);
        }
    }

    const ViewRecord view = *record;
    const OpResult result = m_model.closeView(viewId);

    if (!result.ok) {
        return closeResult(false, result.message);
    }

    releaseResources(viewId, view.typeId, view.resource);
    publish();
    emit viewClosed(viewId, view.typeId, resourceToMap(view.resource));
    return closeResult(true);
}

QVariantMap WorkspaceStore::closeAnchor(const QString &anchorId, bool force)
{
    const AnchorRecord *anchor = m_model.anchor(anchorId);

    if (!anchor) {
        return closeResult(false, QStringLiteral("Unknown panel."));
    }

    if (!force) {
        const QStringList unsaved = unsavedDocumentsForViews(anchor->views);
        if (!unsaved.isEmpty()) {
            return closeResult(false, {}, unsaved);
        }
    }

    QList<ViewRecord> closing;
    for (const QString &viewId : anchor->views) {
        if (const ViewRecord *view = m_model.view(viewId)) {
            closing.append(*view);
        }
    }

    const OpResult result = m_model.closeAnchor(anchorId);

    if (!result.ok) {
        return closeResult(false, result.message);
    }

    for (const ViewRecord &view : closing) {
        releaseResources(view.id, view.typeId, view.resource);
    }
    publish();
    for (const ViewRecord &view : closing) {
        emit viewClosed(view.id, view.typeId, resourceToMap(view.resource));
    }
    return closeResult(true);
}

QVariantMap WorkspaceStore::closeOtherViews(const QString &viewId, bool force)
{
    const AnchorRecord *anchor = m_model.anchor(m_model.anchorOfView(viewId));

    if (!anchor) {
        return closeResult(false, QStringLiteral("Unknown view."));
    }

    QStringList others = anchor->views;
    others.removeAll(viewId);

    if (!force) {
        const QStringList unsaved = unsavedDocumentsForViews(others);
        if (!unsaved.isEmpty()) {
            return closeResult(false, {}, unsaved);
        }
    }

    for (const QString &other : others) {
        closeView(other, true);
    }
    return closeResult(true);
}

bool WorkspaceStore::activateView(const QString &viewId)
{
    const OpResult result = m_model.activateView(viewId);

    if (!result.ok) {
        return false;
    }

    m_model.focusAnchor(m_model.anchorOfView(viewId));
    publish();
    emit viewFocused(viewId);
    return true;
}

bool WorkspaceStore::moveView(const QString &viewId, const QString &anchorId, int index)
{
    return check(m_model.moveView(viewId, anchorId, index));
}

QString WorkspaceStore::splitView(const QString &viewId, const QString &anchorId, const QString &edgeName)
{
    Edge edge = Edge::Right;
    if (!parseEdgeName(edgeName, &edge) || edge == Edge::Center) {
        return {};
    }

    const OpResult result = m_model.splitView(viewId, anchorId.isEmpty() ? m_model.anchorOfView(viewId) : anchorId, edge);
    if (!check(result)) {
        return {};
    }
    return result.createdId;
}

QString WorkspaceStore::duplicateView(const QString &viewId, const QString &edgeName)
{
    Edge edge = Edge::Right;
    if (!parseEdgeName(edgeName, &edge)) {
        return {};
    }

    const OpResult result = m_model.duplicateView(viewId, {}, edge);
    if (!result.ok) {
        emit operationFailed(result.message);
        return {};
    }

    m_model.focusAnchor(m_model.anchorOfView(result.createdId));
    publish();
    emit viewOpened(result.createdId);
    emit viewFocused(result.createdId);
    return result.createdId;
}

bool WorkspaceStore::setViewState(const QString &viewId, const QVariantMap &state)
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return false;
    }
    if (view->state == state) {
        return true;
    }
    return check(m_model.updateViewState(viewId, state));
}

bool WorkspaceStore::mergeViewState(const QString &viewId, const QVariantMap &patch)
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return false;
    }

    QVariantMap state = view->state;
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        state.insert(it.key(), it.value());
    }
    return setViewState(viewId, state);
}

bool WorkspaceStore::setViewTitle(const QString &viewId, const QString &title)
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return false;
    }
    if (view->title == title) {
        return true;
    }
    return check(m_model.setViewTitle(viewId, title));
}

bool WorkspaceStore::setViewType(const QString &viewId, const QString &typeId)
{
    const ViewTypeDescriptor *type = m_registry.find(typeId);
    return check(m_model.setViewType(viewId, typeId, type ? type->stateVersion : 1));
}

bool WorkspaceStore::setViewResource(const QString &viewId, const QVariantMap &resource, const QString &originName)
{
    Origin origin = Origin::User;
    static_cast<void>(parseOrigin(originName, &origin)); // unknown: the person
    const OpResult result = m_model.setViewResource(viewId, resourceFromMap(resource), origin);

    // Automatic rebinding that a lock refuses is expected, not an error.
    if (!result.ok && origin != Origin::User) {
        return false;
    }
    return check(result);
}

QVariantMap WorkspaceStore::view(const QString &viewId) const
{
    const ViewRecord *view = m_model.view(viewId);

    if (!view) {
        return {};
    }

    return {
        {QStringLiteral("viewId"), view->id},
        {QStringLiteral("typeId"), view->typeId},
        {QStringLiteral("title"), view->title},
        {QStringLiteral("resource"), resourceToMap(view->resource)},
        {QStringLiteral("state"), view->state},
        {QStringLiteral("anchorId"), m_model.anchorOfView(viewId)},
        {QStringLiteral("surfaceId"), m_model.surfaceOfAnchor(m_model.anchorOfView(viewId))},
    };
}

QVariantMap WorkspaceStore::viewRow(const QString &viewId) const
{
    return m_views->row(viewId);
}

QStringList WorkspaceStore::viewsForResource(const QString &kind, const QString &id) const
{
    return m_model.viewsForResource(kind, id);
}

QStringList WorkspaceStore::viewIdsOfType(const QString &typeId) const
{
    QStringList ids;
    for (const QString &viewId : m_model.viewIds()) {
        const ViewRecord *view = m_model.view(viewId);
        if (view && view->typeId == typeId) {
            ids.append(viewId);
        }
    }
    return ids;
}

QVariantMap WorkspaceStore::viewType(const QString &typeId) const
{
    const ViewTypeDescriptor *type = m_registry.find(typeId);

    if (!type) {
        return {};
    }

    return {
        {QStringLiteral("typeId"), type->typeId},
        {QStringLiteral("displayName"), type->displayName},
        {QStringLiteral("icon"), type->icon},
        {QStringLiteral("resourceKind"), type->resourceKind},
        {QStringLiteral("allowsDuplicates"), type->allowsDuplicates},
        {QStringLiteral("worktreeBound"), type->worktreeBound},
        {QStringLiteral("stateVersion"), type->stateVersion},
    };
}

// ---- anchors -------------------------------------------------------------

QString WorkspaceStore::createAnchor(const QString &anchorId, const QString &edgeName)
{
    Edge edge = Edge::Right;
    if (!parseEdgeName(edgeName, &edge) || edge == Edge::Center) {
        return {};
    }

    const OpResult result = m_model.createAnchor(anchorId, edge);
    if (!result.ok) {
        emit operationFailed(result.message);
        return {};
    }

    m_model.focusAnchor(result.createdId);
    publish();
    return result.createdId;
}

bool WorkspaceStore::focusAnchor(const QString &anchorId)
{
    if (m_model.focusedAnchorId() == anchorId) {
        return true;
    }

    const bool ok = check(m_model.focusAnchor(anchorId));
    if (ok && !focusedViewId().isEmpty()) {
        emit viewFocused(focusedViewId());
    }
    return ok;
}

bool WorkspaceStore::toggleTargetAnchor(const QString &anchorId)
{
    const bool isTarget = m_model.explicitTargetAnchorId() == anchorId;
    return check(m_model.setTargetAnchor(isTarget ? QString() : anchorId));
}

bool WorkspaceStore::toggleMaximize(const QString &anchorId)
{
    const QString surfaceId = m_model.surfaceOfAnchor(anchorId);
    const SurfaceRecord *surface = m_model.surface(surfaceId);

    if (!surface) {
        return false;
    }

    if (surface->maximizedAnchorId == anchorId) {
        return check(m_model.restoreAnchor(surfaceId));
    }

    const bool ok = check(m_model.maximizeAnchor(anchorId));
    if (ok) {
        m_model.focusAnchor(anchorId);
        publish();
    }
    return ok;
}

bool WorkspaceStore::moveAnchor(const QString &anchorId, const QString &targetAnchorId, const QString &edgeName)
{
    Edge edge = Edge::Center;
    if (!parseEdgeName(edgeName, &edge)) {
        return false;
    }
    return check(m_model.moveAnchor(anchorId, targetAnchorId, edge));
}

bool WorkspaceStore::mergeAnchor(const QString &anchorId, const QString &targetAnchorId)
{
    return check(m_model.mergeAnchor(anchorId, targetAnchorId));
}

bool WorkspaceStore::focusNeighbor(const QString &direction)
{
    Edge edge = Edge::Center;
    const QString current = m_model.focusedAnchorId();

    if (!parseEdgeName(direction, &edge) || edge == Edge::Center || current.isEmpty()) {
        return false;
    }

    const QString surfaceId = m_model.surfaceOfAnchor(current);
    const QString neighbor = m_model.neighborAnchor(current, edge, surfaceSize(surfaceId), m_metrics);

    if (neighbor.isEmpty()) {
        return false;
    }

    // Moving focus off a maximized anchor restores the layout first.
    const SurfaceRecord *surface = m_model.surface(surfaceId);
    if (surface && !surface->maximizedAnchorId.isEmpty()) {
        m_model.restoreAnchor(surfaceId);
    }

    return focusAnchor(neighbor);
}

bool WorkspaceStore::cycleFocus(int step)
{
    const QString next = m_model.cycleAnchor(m_model.focusedAnchorId(), step);
    return !next.isEmpty() && focusAnchor(next);
}

bool WorkspaceStore::cycleView(int step)
{
    const AnchorRecord *anchor = m_model.anchor(m_model.focusedAnchorId());

    if (!anchor || anchor->views.size() < 2) {
        return false;
    }

    const int current = int(anchor->views.indexOf(anchor->activeViewId));
    const int count = int(anchor->views.size());
    const int next = ((current + step) % count + count) % count;
    return activateView(anchor->views.at(next));
}

QVariantMap WorkspaceStore::anchor(const QString &anchorId) const
{
    const AnchorRecord *anchor = m_model.anchor(anchorId);

    if (!anchor) {
        return {};
    }

    const QString surfaceId = m_model.surfaceOfAnchor(anchorId);
    const SurfaceRecord *surface = m_model.surface(surfaceId);

    return {
        {QStringLiteral("anchorId"), anchorId},
        {QStringLiteral("surfaceId"), surfaceId},
        {QStringLiteral("views"), anchor->views},
        {QStringLiteral("activeViewId"), anchor->activeViewId},
        {QStringLiteral("pinned"), anchor->pinned},
        {QStringLiteral("roles"), anchor->roles},
        {QStringLiteral("maximized"), surface && surface->maximizedAnchorId == anchorId},
        {QStringLiteral("name"), anchor->name},
        {QStringLiteral("displayName"), m_model.anchorDisplayName(anchorId)},
        {QStringLiteral("automaticName"), m_model.automaticAnchorName(anchorId)},
        {QStringLiteral("layoutLocked"), anchor->layoutLocked},
        {QStringLiteral("contentLocked"), anchor->contentLocked},
        {QStringLiteral("switchable"), anchor->switchable},
        {QStringLiteral("libraryId"), anchor->libraryId},
    };
}

bool WorkspaceStore::renameAnchor(const QString &anchorId, const QString &name)
{
    return check(m_model.renameAnchor(anchorId, name));
}

bool WorkspaceStore::setAnchorLayoutLocked(const QString &anchorId, bool locked)
{
    return check(m_model.setAnchorLayoutLocked(anchorId, locked));
}

bool WorkspaceStore::setAnchorContentLocked(const QString &anchorId, bool locked)
{
    return check(m_model.setAnchorContentLocked(anchorId, locked));
}

bool WorkspaceStore::setAnchorSwitchable(const QString &anchorId, bool switchable)
{
    return check(m_model.setAnchorSwitchable(anchorId, switchable));
}

bool WorkspaceStore::setAnchorLibrary(const QString &anchorId, const QString &libraryId)
{
    return check(m_model.setAnchorLibrary(anchorId, libraryId));
}

QString WorkspaceStore::anchorDisplayName(const QString &anchorId) const
{
    return m_model.anchorDisplayName(anchorId);
}

QVariantMap WorkspaceStore::anchorCapabilities(const QString &anchorId) const
{
    const AnchorCapabilities capabilities = m_model.anchorCapabilities(anchorId, Origin::User);
    QVariantMap map = Commands::capabilitiesMap(capabilities);
    QVariantMap restrictions;
    for (auto it = capabilities.reasons.cbegin(); it != capabilities.reasons.cend(); ++it) {
        restrictions.insert(it.key(), it.value());
    }
    map.insert(QStringLiteral("restrictions"), restrictions);
    return map;
}

QVariantMap WorkspaceStore::resolveLibrary(
    const QString &anchorId,
    const QString &explorerLibraryId,
    const QString &defaultLibraryId,
    const QStringList &knownLibraryIds
) const
{
    const LibraryResolution resolution = m_model.resolveLibrary(anchorId, explorerLibraryId, defaultLibraryId, knownLibraryIds);
    return {
        {QStringLiteral("libraryId"), resolution.libraryId},
        {QStringLiteral("source"), librarySourceName(resolution.source)},
    };
}

QVariantMap WorkspaceStore::runCommand(const QVariantMap &command)
{
    Origin origin = Origin::Agent;
    if (command.contains(QStringLiteral("origin")) && !parseOrigin(command.value(QStringLiteral("origin")).toString(), &origin)) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("code"), QStringLiteral("INVALID_ARGUMENT")}, {QStringLiteral("error"), QStringLiteral("Unknown origin.")}};
    }

    const QString name = command.value(QStringLiteral("command")).toString();

    // Closing a view goes through the same unsaved-work guard as the UI.
    if (name == QStringLiteral("anchor.removeResource")) {
        const QString viewId = command.value(QStringLiteral("viewId")).toString();
        const bool force = origin == Origin::User && command.value(QStringLiteral("force")).toBool();
        const QStringList unsaved = unsavedDocumentsForViews({viewId});
        if (!force && !unsaved.isEmpty()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("UNSAVED_WORK")},
                {QStringLiteral("error"), QStringLiteral("That view holds the only open copy of unsaved edits.")},
                {QStringLiteral("documents"), unsaved},
            };
        }
        const ViewRecord *record = m_model.view(viewId);
        const ViewRecord view = record ? *record : ViewRecord();
        const Commands::Result result = Commands::execute(m_model, command, origin);
        if (result.op.ok) {
            releaseResources(viewId, view.typeId, view.resource);
            publish();
            emit viewClosed(viewId, view.typeId, resourceToMap(view.resource));
        }
        return Commands::resultMap(result);
    }

    const Commands::Result result = Commands::execute(m_model, command, origin);

    if (result.op.ok && !Commands::isQuery(name)) {
        publish();
        const QString opened = result.data.value(QStringLiteral("viewId")).toString();
        if (!opened.isEmpty()) {
            emit viewOpened(opened);
        }
    }

    return Commands::resultMap(result);
}

QString WorkspaceStore::preferredAnchorFor(const QString &typeId) const
{
    Placement::Hints hints;
    hints.recentAnchors = m_recentAnchors;
    return Placement::chooseAnchor(m_model, typeId, {}, hints);
}

QVariantMap WorkspaceStore::tabStripAt(const QString &surfaceId, double x, double y, double stripHeight) const
{
    const LayoutGeometry geometry = m_model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics);

    for (auto it = geometry.anchors.cbegin(); it != geometry.anchors.cend(); ++it) {
        const QRectF strip(it.value().left(), it.value().top(), it.value().width(), stripHeight);
        if (strip.contains(QPointF(x, y))) {
            return {
                {QStringLiteral("anchorId"), it.key()},
                {QStringLiteral("localX"), x - it.value().left()},
            };
        }
    }

    return {};
}

// ---- splits --------------------------------------------------------------

bool WorkspaceStore::resizeSplit(const QString &surfaceId, const QString &nodeId, int index, double position)
{
    const OpResult result = m_model.resizeSplit(surfaceId, nodeId, index, position, surfaceSize(surfaceId), m_metrics);

    if (!result.ok) {
        return false;
    }

    publish();
    return true;
}

bool WorkspaceStore::equalizeSplit(const QString &nodeId)
{
    return check(m_model.equalizeSplit(nodeId));
}

void WorkspaceStore::setResizing(bool resizing)
{
    if (m_resizing == resizing) {
        return;
    }

    m_resizing = resizing;
    if (!resizing) {
        scheduleSave();
    }
}

// ---- drags ---------------------------------------------------------------

bool WorkspaceStore::beginDrag(const QString &kind, const QString &id)
{
    DragSource source;

    if (kind == QStringLiteral("view") && m_model.view(id)) {
        source = DragSource::view(id);
    } else if (kind == QStringLiteral("anchor") && m_model.anchor(id)) {
        source = DragSource::anchor(id);
    } else {
        return false;
    }

    m_drag = source;
    m_dropTarget = {};
    m_dropAllowed = false;
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();
    publish();
    emit dragChanged();
    return true;
}

void WorkspaceStore::updateDrag(const QString &surfaceId, double x, double y)
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }

    // Hit-test against the committed layout, never the preview, so the
    // target does not jump as panels ease aside.
    setDropTarget(m_model.dropTargetAt(surfaceId, QPointF(x, y), surfaceSize(surfaceId), m_metrics));
}

void WorkspaceStore::updateDragOverTabs(const QString &surfaceId, const QString &anchorId, int index)
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }

    DropTarget target;
    target.surfaceId = surfaceId;
    target.anchorId = anchorId;
    target.edge = Edge::Center;
    target.index = index;
    setDropTarget(target);
}

void WorkspaceStore::clearDragTarget()
{
    const bool wasDetached = m_dragDetached;
    m_dragDetached = false;
    setDropTarget({});
    if (wasDetached) {
        emit dragChanged();
    }
}

bool WorkspaceStore::canDetach(const DragSource &source) const
{
    // A panel that is already alone in its own window would only become
    // another identical window: move the window instead.
    const QString anchorId = source.kind == DragSource::Kind::View ? m_model.anchorOfView(source.id) : source.id;
    const AnchorRecord *anchor = m_model.anchor(anchorId);
    const QString surfaceId = m_model.surfaceOfAnchor(anchorId);
    const SurfaceRecord *surface = m_model.surface(surfaceId);

    if (!anchor || !surface) {
        return false;
    }
    if (surface->primary) {
        return true;
    }

    const bool alone = m_model.anchorIds(surfaceId).size() == 1;
    return !(alone && (source.kind == DragSource::Kind::Anchor || anchor->views.size() == 1));
}

void WorkspaceStore::updateDragDetached(double x, double y, double width, double height, const QString &screenName)
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }

    const QRectF rect(x, y, std::max(320.0, width), std::max(220.0, height));
    const bool allowed = canDetach(m_drag);

    if (m_dragDetached && m_detachRect == rect && m_detachScreen == screenName && m_dropAllowed == allowed) {
        return;
    }

    const bool hadTarget = m_dropTarget.isValid();
    m_dropTarget = {};
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();
    m_dragDetached = true;
    m_detachRect = rect;
    m_detachScreen = screenName;
    m_dropAllowed = allowed;

    if (hadTarget) {
        publish();
    }
    emit dragChanged();
}

QVariantMap WorkspaceStore::detachRect() const
{
    if (!m_dragDetached) {
        return {};
    }
    return {
        {QStringLiteral("x"), m_detachRect.x()},
        {QStringLiteral("y"), m_detachRect.y()},
        {QStringLiteral("width"), m_detachRect.width()},
        {QStringLiteral("height"), m_detachRect.height()},
        {QStringLiteral("screen"), m_detachScreen},
    };
}

void WorkspaceStore::setDropTarget(const DropTarget &target)
{
    if (target.surfaceId == m_dropTarget.surfaceId && target.anchorId == m_dropTarget.anchorId
        && target.edge == m_dropTarget.edge && target.index == m_dropTarget.index) {
        return;
    }

    m_dragDetached = false;
    m_dropTarget = target;
    m_dropAllowed = target.isValid() && m_model.canDrop(m_drag, target).ok;
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();

    if (m_dropAllowed) {
        m_preview = m_model.previewDrop(m_drag, target, surfaceSize(target.surfaceId), m_metrics, &m_landingRect);
        m_previewSurfaceId = target.surfaceId;
    }

    publish();
    emit dragChanged();
}

void WorkspaceStore::resetDrag()
{
    m_drag = {};
    m_dropTarget = {};
    m_dropAllowed = false;
    m_dragDetached = false;
    m_detachRect = {};
    m_detachScreen.clear();
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();
}

bool WorkspaceStore::finishDrag()
{
    if (m_drag.kind == DragSource::Kind::None) {
        return false;
    }

    const DragSource source = m_drag;
    const DropTarget target = m_dropTarget;
    const bool allowed = m_dropAllowed;
    const bool detached = m_dragDetached;
    const QRectF detachRect = m_detachRect;
    const QString detachScreen = m_detachScreen;

    resetDrag();

    bool ok = false;

    if (allowed && detached) {
        const OpResult result = source.kind == DragSource::Kind::View
            ? m_model.detachView(source.id, detachRect, detachScreen)
            : m_model.detachAnchor(source.id, detachRect, detachScreen);
        ok = result.ok;
        if (!ok) {
            emit operationFailed(result.message);
        }
    } else if (allowed) {
        const OpResult result = m_model.applyDrop(source, target);
        ok = result.ok;

        if (ok) {
            const QString anchorId = source.kind == DragSource::Kind::View
                ? m_model.anchorOfView(source.id)
                : (m_model.anchor(source.id) ? source.id : target.anchorId);
            if (source.kind == DragSource::Kind::View) {
                m_model.activateView(source.id);
            }
            m_model.focusAnchor(anchorId);
        } else {
            emit operationFailed(result.message);
        }
    }

    publish();
    emit dragChanged();
    return ok;
}

void WorkspaceStore::cancelDrag()
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }

    resetDrag();
    publish();
    emit dragChanged();
}

// ---- surfaces ------------------------------------------------------------

QStringList WorkspaceStore::floatingSurfaceIds() const
{
    QStringList ids;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        const SurfaceRecord *surface = m_model.surface(surfaceId);
        if (surface && !surface->primary) {
            ids.append(surfaceId);
        }
    }
    return ids;
}

QVariantMap WorkspaceStore::surface(const QString &surfaceId) const
{
    const SurfaceRecord *record = m_model.surface(surfaceId);
    if (!record) {
        return {};
    }

    int viewCount = 0;
    const QStringList anchorIds = m_model.anchorIds(surfaceId);
    for (const QString &anchorId : anchorIds) {
        if (const AnchorRecord *anchor = m_model.anchor(anchorId)) {
            viewCount += int(anchor->views.size());
        }
    }

    return {
        {QStringLiteral("surfaceId"), record->id},
        {QStringLiteral("primary"), record->primary},
        {QStringLiteral("x"), record->geometry.x()},
        {QStringLiteral("y"), record->geometry.y()},
        {QStringLiteral("width"), record->geometry.width()},
        {QStringLiteral("height"), record->geometry.height()},
        {QStringLiteral("hasGeometry"), record->geometry.isValid()},
        {QStringLiteral("screen"), record->screenName},
        {QStringLiteral("windowMaximized"), record->windowMaximized},
        {QStringLiteral("anchorCount"), int(anchorIds.size())},
        {QStringLiteral("viewCount"), viewCount},
    };
}

bool WorkspaceStore::setSurfaceGeometry(
    const QString &surfaceId,
    double x,
    double y,
    double width,
    double height,
    const QString &screenName,
    bool windowMaximized
)
{
    const SurfaceRecord *record = m_model.surface(surfaceId);
    if (!record || record->primary) {
        return false;
    }

    const QRectF geometry(x, y, width, height);
    if (record->windowMaximized != windowMaximized) {
        m_model.setSurfaceWindowMaximized(surfaceId, windowMaximized);
    }
    // While maximized the frame is the screen's; keep the restored frame.
    if (!windowMaximized && geometry.isValid() && (record->geometry != geometry || record->screenName != screenName)) {
        m_model.setSurfaceGeometry(surfaceId, geometry, screenName, true);
    }

    // Geometry is not layout: save it without republishing every panel.
    if (m_publishedModelRevision != m_model.revision()) {
        m_publishedModelRevision = m_model.revision();
        scheduleSave();
    }
    return true;
}

QString WorkspaceStore::detachView(const QString &viewId, double x, double y, double width, double height, const QString &screenName)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const OpResult result = m_model.detachView(viewId, QRectF(x, y, width, height), screenName);
    return check(result) ? result.createdId : QString();
}

QString WorkspaceStore::detachAnchor(const QString &anchorId, double x, double y, double width, double height, const QString &screenName)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const OpResult result = m_model.detachAnchor(anchorId, QRectF(x, y, width, height), screenName);
    return check(result) ? result.createdId : QString();
}

bool WorkspaceStore::closeSurface(const QString &surfaceId)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    return check(m_model.closeSurface(surfaceId));
}

QList<ScreenInfo> WorkspaceStore::currentScreens() const
{
    if (m_useTestScreens) {
        return m_testScreens;
    }

    QList<ScreenInfo> screens;
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return screens;
    }

    const QScreen *primary = QGuiApplication::primaryScreen();
    for (const QScreen *screen : QGuiApplication::screens()) {
        ScreenInfo info;
        info.name = screen->name();
        info.geometry = screen->geometry();
        info.availableGeometry = screen->availableGeometry();
        info.devicePixelRatio = screen->devicePixelRatio();
        info.primary = screen == primary;
        screens.append(info);
    }
    return screens;
}

void WorkspaceStore::setScreensForTesting(const QList<ScreenInfo> &screens)
{
    m_testScreens = screens;
    m_useTestScreens = true;
}

QStringList WorkspaceStore::recoverSurfaces()
{
    const QStringList moved = m_model.recoverSurfaces(currentScreens());
    if (!moved.isEmpty()) {
        publish();
        for (const QString &surfaceId : moved) {
            emit surfaceRecovered(surfaceId);
        }
    }
    return moved;
}

// ---- layouts -------------------------------------------------------------

bool WorkspaceStore::applyPreset(const QString &presetId)
{
    return applyLayout(presetId);
}

QJsonArray WorkspaceStore::readSavedLayouts() const
{
    const QString text = m_settings.value(QStringLiteral("workspace/docking/layouts")).toString();
    return QJsonDocument::fromJson(text.toUtf8()).array();
}

void WorkspaceStore::writeSavedLayouts(const QJsonArray &layouts)
{
    m_settings.setValue(QStringLiteral("workspace/docking/layouts"), QString::fromUtf8(QJsonDocument(layouts).toJson(QJsonDocument::Compact)));
    emit layoutsChanged();
}

QVariantList WorkspaceStore::savedLayouts() const
{
    QVariantList list;

    for (const QJsonValue &value : readSavedLayouts()) {
        const QJsonObject entry = value.toObject();
        list.append(QVariantMap{
            {QStringLiteral("id"), entry.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), entry.value(QStringLiteral("name")).toString()},
            {QStringLiteral("savedAt"), entry.value(QStringLiteral("savedAt")).toString()},
        });
    }

    return list;
}

QJsonObject WorkspaceStore::layoutDocument(const QString &layoutId) const
{
    const QJsonObject preset = Presets::layout(layoutId);

    if (!preset.isEmpty()) {
        return preset;
    }

    for (const QJsonValue &value : readSavedLayouts()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == layoutId) {
            return entry.value(QStringLiteral("layout")).toObject();
        }
    }

    return {};
}

QString WorkspaceStore::currentLayoutName() const
{
    if (m_currentLayoutId.isEmpty()) {
        return {};
    }

    for (const Presets::Preset &preset : Presets::all()) {
        if (preset.id == m_currentLayoutId) {
            return preset.name;
        }
    }

    for (const QJsonValue &value : readSavedLayouts()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == m_currentLayoutId) {
            return entry.value(QStringLiteral("name")).toString();
        }
    }

    return {};
}

void WorkspaceStore::setCurrentLayoutId(const QString &layoutId)
{
    m_currentLayoutId = layoutId;

    if (!m_collectionId.isEmpty()) {
        m_settings.setValue(QStringLiteral("workspace/collections/%1/docking/layoutId").arg(m_collectionId), layoutId);
    }

    emit layoutsChanged();
}

bool WorkspaceStore::applyLayoutDocument(const QJsonObject &layout, const QString &layoutId)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }

    const QJsonObject before = Codec::captureLayout(m_model, QString());
    const QString beforeId = m_currentLayoutId;
    const Codec::LoadReport report = Codec::applyLayout(layout, m_model);

    if (!report.ok) {
        emit operationFailed(report.error);
        return false;
    }

    m_layoutUndo = before;
    m_layoutUndoId = beforeId;
    setCurrentLayoutId(layoutId);
    publish();
    return true;
}

bool WorkspaceStore::applyLayout(const QString &layoutId)
{
    const QJsonObject layout = layoutDocument(layoutId);

    if (layout.isEmpty()) {
        emit operationFailed(QStringLiteral("Unknown layout."));
        return false;
    }

    if (!applyLayoutDocument(layout, layoutId)) {
        return false;
    }

    emit layoutApplied(layoutId, currentLayoutName());
    return true;
}

bool WorkspaceStore::undoLayout()
{
    if (m_layoutUndo.isEmpty()) {
        return false;
    }

    const QJsonObject layout = m_layoutUndo;
    const QString layoutId = m_layoutUndoId;
    const bool ok = applyLayoutDocument(layout, layoutId);

    if (ok) {
        // Undoing an undo is just applying the layout again from the picker.
        m_layoutUndo = {};
        m_layoutUndoId.clear();
        emit layoutsChanged();
    }

    return ok;
}

QString WorkspaceStore::saveLayout(const QString &name)
{
    const QString trimmed = name.trimmed();

    if (trimmed.isEmpty()) {
        emit operationFailed(QStringLiteral("A layout needs a name."));
        return {};
    }

    QJsonArray layouts = readSavedLayouts();
    QString id;

    // Saving under an existing name replaces that layout.
    for (int index = 0; index < layouts.size(); ++index) {
        if (layouts.at(index).toObject().value(QStringLiteral("name")).toString().compare(trimmed, Qt::CaseInsensitive) == 0) {
            id = layouts.at(index).toObject().value(QStringLiteral("id")).toString();
            layouts.removeAt(index);
            break;
        }
    }

    if (id.isEmpty()) {
        id = QStringLiteral("user.") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    }

    layouts.append(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), trimmed},
        {QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("layout"), Codec::captureLayout(m_model, trimmed)},
    });
    writeSavedLayouts(layouts);
    setCurrentLayoutId(id);
    return id;
}

bool WorkspaceStore::renameLayout(const QString &layoutId, const QString &name)
{
    const QString trimmed = name.trimmed();
    QJsonArray layouts = readSavedLayouts();

    if (trimmed.isEmpty()) {
        return false;
    }

    for (int index = 0; index < layouts.size(); ++index) {
        QJsonObject entry = layouts.at(index).toObject();
        if (entry.value(QStringLiteral("id")).toString() == layoutId) {
            entry.insert(QStringLiteral("name"), trimmed);
            QJsonObject layout = entry.value(QStringLiteral("layout")).toObject();
            layout.insert(QStringLiteral("name"), trimmed);
            entry.insert(QStringLiteral("layout"), layout);
            layouts.replace(index, entry);
            writeSavedLayouts(layouts);
            return true;
        }
    }

    return false;
}

bool WorkspaceStore::deleteLayout(const QString &layoutId)
{
    QJsonArray layouts = readSavedLayouts();

    for (int index = 0; index < layouts.size(); ++index) {
        if (layouts.at(index).toObject().value(QStringLiteral("id")).toString() == layoutId) {
            layouts.removeAt(index);
            writeSavedLayouts(layouts);
            if (m_currentLayoutId == layoutId) {
                setCurrentLayoutId(QString());
            }
            return true;
        }
    }

    return false;
}

QVariantList WorkspaceStore::layoutPreview(const QString &layoutId) const
{
    WorkspaceModel temp;
    const QJsonObject layout = layoutId.isEmpty() ? Codec::captureLayout(m_model, QString()) : layoutDocument(layoutId);

    if (layout.isEmpty() || !Codec::applyLayout(layout, temp).ok) {
        return {};
    }

    const QSizeF size(1000, 640);
    LayoutMetrics metrics;
    metrics.minimumAnchorWidth = 1;
    metrics.minimumAnchorHeight = 1;
    metrics.splitterThickness = 16;
    const QString surfaceId = temp.primarySurfaceId();
    const LayoutGeometry geometry = temp.computeGeometry(surfaceId, size, metrics);
    QVariantList rects;

    const auto kindOf = [](const QStringList &types) -> QString {
        static const QList<QPair<QString, QString>> order = {
            {QStringLiteral("archivist.chat"), QStringLiteral("chat")},
            {QStringLiteral("archivist.editor"), QStringLiteral("editor")},
            {QStringLiteral("archivist.diff"), QStringLiteral("diff")},
            {QStringLiteral("archivist.preview"), QStringLiteral("preview")},
            {QStringLiteral("archivist.terminal"), QStringLiteral("terminal")},
        };
        for (const auto &entry : order) {
            if (types.contains(entry.first)) {
                return entry.second;
            }
        }
        return QStringLiteral("any");
    };

    for (const QString &anchorId : temp.anchorIds(surfaceId)) {
        const AnchorRecord *anchor = temp.anchor(anchorId);
        QStringList types = anchor ? anchor->roles : QStringList();

        // Panels without roles: describe them by what they hold now.
        if (types.isEmpty()) {
            if (const AnchorRecord *live = m_model.anchor(anchorId)) {
                for (const QString &viewId : live->views) {
                    if (const ViewRecord *view = m_model.view(viewId)) {
                        types.append(view->typeId);
                    }
                }
            }
        }

        const QRectF rect = geometry.anchors.value(anchorId);
        rects.append(QVariantMap{
            {QStringLiteral("x"), rect.x() / size.width()},
            {QStringLiteral("y"), rect.y() / size.height()},
            {QStringLiteral("width"), rect.width() / size.width()},
            {QStringLiteral("height"), rect.height() / size.height()},
            {QStringLiteral("kind"), kindOf(types)},
        });
    }

    return rects;
}
