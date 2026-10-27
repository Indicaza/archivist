#include "App/Domains/Workspace/workspace_store.h"

#include "App/Domains/Documents/document_store.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_presets.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

using namespace Workspace;

namespace
{
const QStringList kAnchorRoles = {
    QStringLiteral("anchorId"),
    QStringLiteral("surfaceId"),
    QStringLiteral("x"),
    QStringLiteral("y"),
    QStringLiteral("width"),
    QStringLiteral("height"),
    QStringLiteral("views"),
    QStringLiteral("activeViewId"),
    QStringLiteral("activeTypeId"),
    QStringLiteral("pinned"),
    QStringLiteral("roles"),
    QStringLiteral("focused"),
    QStringLiteral("target"),
    QStringLiteral("maximized"),
    QStringLiteral("hidden"),
    QStringLiteral("dragSource"),
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
    QStringLiteral("state"),
    QStringLiteral("anchorId"),
    QStringLiteral("surfaceId"),
    QStringLiteral("index"),
    QStringLiteral("active"),
    QStringLiteral("focused"),
    QStringLiteral("x"),
    QStringLiteral("y"),
    QStringLiteral("width"),
    QStringLiteral("height"),
    QStringLiteral("hidden"),
};

const QStringList kSplitterRoles = {
    QStringLiteral("key"),
    QStringLiteral("nodeId"),
    QStringLiteral("index"),
    QStringLiteral("orientation"),
    QStringLiteral("x"),
    QStringLiteral("y"),
    QStringLiteral("width"),
    QStringLiteral("height"),
};

QVariantMap rectMap(const QRectF &rect)
{
    return {
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    };
}

bool isDocumentView(const ViewRecord &view)
{
    return view.typeId == ViewTypes::Editor && view.resource.kind == QStringLiteral("document")
        && !view.resource.id.isEmpty();
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

    QVariantMap map = rectMap(m_landingRect);
    map.insert(QStringLiteral("surfaceId"), m_dropTarget.surfaceId);
    return map;
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
    const QSizeF size = m_surfaceSizes.value(surfaceId);
    return size.isEmpty() ? QSizeF(1200, 800) : size;
}

// ---- publishing ----------------------------------------------------------

void WorkspaceStore::publish()
{
    QList<QVariantMap> viewRows;
    const QString focusedAnchor = m_model.focusedAnchorId();
    const QString target = m_model.targetAnchorId();
    QHash<QString, LayoutGeometry> geometries;

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

                QVariantMap row = rectMap(rect);
                row.insert(QStringLiteral("viewId"), view->id);
                row.insert(QStringLiteral("typeId"), view->typeId);
                row.insert(QStringLiteral("title"), view->title);
                row.insert(QStringLiteral("resourceKind"), view->resource.kind);
                row.insert(QStringLiteral("resourceId"), view->resource.id);
                row.insert(QStringLiteral("collectionId"), view->resource.collectionId);
                row.insert(QStringLiteral("libraryId"), view->resource.libraryId);
                row.insert(QStringLiteral("details"), view->resource.details);
                row.insert(QStringLiteral("state"), view->state);
                row.insert(QStringLiteral("anchorId"), anchorId);
                row.insert(QStringLiteral("surfaceId"), surfaceId);
                row.insert(QStringLiteral("index"), index);
                row.insert(QStringLiteral("active"), anchor->activeViewId == view->id);
                row.insert(QStringLiteral("focused"), anchor->activeViewId == view->id && focusedAnchor == anchorId);
                row.insert(QStringLiteral("hidden"), hidden);
                viewRows.append(row);
            }
        }
    }

    m_views->setRows(viewRows);

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

    const bool modelChanged = m_publishedModelRevision != m_model.revision();
    m_publishedModelRevision = m_model.revision();
    ++m_revision;
    emit revisionChanged();

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

        if (surface) {
            for (const QString &anchorId : m_model.anchorIds(surfaceId)) {
                const AnchorRecord *anchor = m_model.anchor(anchorId);
                const ViewRecord *active = m_model.view(anchor->activeViewId);
                QVariantMap row = rectMap(geometry.anchors.value(anchorId));
                row.insert(QStringLiteral("anchorId"), anchorId);
                row.insert(QStringLiteral("surfaceId"), surfaceId);
                row.insert(QStringLiteral("views"), anchor->views);
                row.insert(QStringLiteral("activeViewId"), anchor->activeViewId);
                row.insert(QStringLiteral("activeTypeId"), active ? active->typeId : QString());
                row.insert(QStringLiteral("pinned"), anchor->pinned);
                row.insert(QStringLiteral("roles"), anchor->roles);
                row.insert(QStringLiteral("focused"), anchorId == focusedAnchor);
                row.insert(QStringLiteral("target"), anchorId == target);
                row.insert(QStringLiteral("maximized"), surface->maximizedAnchorId == anchorId);
                row.insert(QStringLiteral("hidden"), !geometry.anchors.contains(anchorId));
                row.insert(
                    QStringLiteral("dragSource"),
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
                row.insert(QStringLiteral("index"), handle.index);
                row.insert(QStringLiteral("orientation"), orientationName(handle.orientation));
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
    emit collectionIdChanged();
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
    const QString anchorId = options.value(QStringLiteral("anchorId")).toString();

    if (reveal && !resource.isEmpty()) {
        // Prefer a view in the requested (or target) anchor, then any.
        const QString preferredAnchor = anchorId.isEmpty() ? m_model.targetAnchorId() : anchorId;
        QString existing;

        for (const QString &viewId : m_model.viewsForResource(resource.kind, resource.id)) {
            const ViewRecord *view = m_model.view(viewId);
            if (!view || view->typeId != typeId) {
                continue;
            }
            if (existing.isEmpty() || m_model.anchorOfView(viewId) == preferredAnchor) {
                existing = viewId;
            }
        }

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
        options.value(QStringLiteral("activate"), true).toBool()
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

        if (documentId.isEmpty() || documents.contains(documentId) || !m_documents->isDirty(documentId)) {
            continue;
        }

        bool shownElsewhere = false;

        for (auto it = m_retained.cbegin(); it != m_retained.cend(); ++it) {
            if (it.value() == documentId && !closing.contains(it.key())) {
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

bool WorkspaceStore::setViewResource(const QString &viewId, const QVariantMap &resource)
{
    return check(m_model.setViewResource(viewId, resourceFromMap(resource)));
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
    };
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
    setDropTarget({});
}

void WorkspaceStore::setDropTarget(const DropTarget &target)
{
    if (target.surfaceId == m_dropTarget.surfaceId && target.anchorId == m_dropTarget.anchorId
        && target.edge == m_dropTarget.edge && target.index == m_dropTarget.index) {
        return;
    }

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

bool WorkspaceStore::finishDrag()
{
    if (m_drag.kind == DragSource::Kind::None) {
        return false;
    }

    const DragSource source = m_drag;
    const DropTarget target = m_dropTarget;
    const bool allowed = m_dropAllowed;

    m_drag = {};
    m_dropTarget = {};
    m_dropAllowed = false;
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();

    bool ok = false;

    if (allowed) {
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

    m_drag = {};
    m_dropTarget = {};
    m_dropAllowed = false;
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();
    publish();
    emit dragChanged();
}

// ---- layouts -------------------------------------------------------------

bool WorkspaceStore::applyPreset(const QString &presetId)
{
    const QJsonObject layout = Presets::layout(presetId);

    if (layout.isEmpty()) {
        emit operationFailed(QStringLiteral("Unknown layout."));
        return false;
    }

    const Codec::LoadReport report = Codec::applyLayout(layout, m_model);

    if (!report.ok) {
        emit operationFailed(report.error);
        return false;
    }

    publish();
    return true;
}
