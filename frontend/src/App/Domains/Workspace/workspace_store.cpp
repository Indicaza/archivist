#include "App/Domains/Workspace/workspace_store.h"

#include "App/Domains/Documents/document_store.h"
#include "Core/Domains/Observatory/live_events.h"
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

#include <cmath>

Q_LOGGING_CATEGORY(lcWellTiming, "archivist.wells.timing", QtWarningMsg)

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
    // A magnet drag ends here however it ends (docked, put away, refused, cancelled).
    connect(this, &WorkspaceStore::dragFinished, this, [this](const QString &outcome, const QVariantMap &details) {
        if (m_liveDrag) {
            const QVariantMap target = details.value(QStringLiteral("target")).toMap();
            LiveEvents::end(m_liveDrag, outcome == QLatin1String("committed") ? 1 : 0, {
                {QStringLiteral("outcome"), outcome},
                {QStringLiteral("into"), target.isEmpty() ? QStringLiteral("none") : target.firstKey()},
            });
            m_liveDrag = 0;
        }
    });
    m_aimTimer.setSingleShot(true);
    connect(&m_aimTimer, &QTimer::timeout, this, [this]() {
        if (m_drag.kind != DragSource::Kind::None && m_aimTarget.isValid()) {
            const DropTarget target = m_aimTarget;
            m_aimTarget = {};
            setDropTarget(target);
        }
    });

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
    // Where publishing spends its time, QML's reactions included (they run
    // inside these signals). Off unless archivist.wells.timing is on.
    // The same stages go to the Observatory live recorder while it records.
    const bool logTiming = lcWellTiming().isDebugEnabled();
    const qint64 liveStart = LiveEvents::recording() ? LiveEvents::nowNs() : -1;
    const bool timing = logTiming || liveStart >= 0;
    QElapsedTimer clock;
    if (timing) {
        clock.start();
    }
    qint64 stamp = 0;
    QStringList stages;
    QVariantMap stageMs;
    auto lap = [&](const char *stage) {
        if (!timing) {
            return;
        }
        const qint64 now = clock.nsecsElapsed();
        const double ms = (now - stamp) / 1e6;
        stages.append(QStringLiteral("%1 %2").arg(QLatin1String(stage)).arg(ms, 0, 'f', 2));
        stageMs.insert(QLatin1String(stage), std::round(ms * 100) / 100);
        stamp = now;
    };
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

    // Views put away in a Gravity Well keep a row (hidden, in no window), so
    // their live content (a terminal's page, a preview) stays alive, parked
    // like a background tab's, instead of being torn down and rebuilt.
    for (const QString &viewId : m_model.minimizedViewIds()) {
        const ViewRecord *view = m_model.view(viewId);
        if (!view) {
            continue;
        }
        // Its last place, as for a hidden tab (never a zero size, which
        // would squeeze a terminal); after a restart, a plain default.
        const QRectF last = m_lastViewRects.value(view->id, QRectF(0, 0, 800, 520));
        QVariantMap row = rectMap(last);
        lastRects.insert(view->id, last);
        row.insert(QStringLiteral("viewId"), view->id);
        row.insert(QStringLiteral("typeId"), view->typeId);
        row.insert(QStringLiteral("title"), view->title);
        row.insert(QStringLiteral("resourceKind"), view->resource.kind);
        row.insert(QStringLiteral("resourceId"), view->resource.id);
        row.insert(QStringLiteral("collectionId"), view->resource.collectionId);
        row.insert(QStringLiteral("libraryId"), view->resource.libraryId);
        row.insert(QStringLiteral("details"), view->resource.details);
        row.insert(QStringLiteral("viewState"), view->state);
        row.insert(QStringLiteral("anchorId"), QString());
        row.insert(QStringLiteral("surfaceId"), QString());
        row.insert(QStringLiteral("tabIndex"), -1);
        row.insert(QStringLiteral("isActive"), false);
        row.insert(QStringLiteral("isFocused"), false);
        row.insert(QStringLiteral("isHidden"), true);
        viewRows.append(row);
    }

    lap("geometry");
    m_views->setRows(viewRows);
    m_lastViewRects = lastRects;
    lap("views");

    for (auto it = m_anchorModels.cbegin(); it != m_anchorModels.cend(); ++it) {
        publishSurface(it.key());
    }
    for (auto it = m_splitterModels.cbegin(); it != m_splitterModels.cend(); ++it) {
        if (!m_anchorModels.contains(it.key())) {
            publishSurface(it.key());
        }
    }

    lap("panels");

    // Keep document buffers in step with the editor views that show them.
    QHash<QString, QString> wanted; // view -> document

    // A minimized editor keeps its buffer (and its unsaved changes).
    for (const QString &viewId : m_model.viewIds() + m_model.minimizedViewIds()) {
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
    lap("documents");

    const bool modelChanged = m_publishedModelRevision != m_model.revision();
    m_publishedModelRevision = m_model.revision();
    ++m_revision;
    emit revisionChanged();
    lap("revision");

    const QStringList floating = floatingSurfaceIds() + QStringList{QStringLiteral("|")} + panelSurfaceIds()
        + QStringList{QStringLiteral("|")} + sidebarSurfaceIds();
    if (floating != m_publishedSurfaces) {
        m_publishedSurfaces = floating;
        emit surfacesChanged();
    }

    lap("surfaces");

    if (modelChanged) {
        scheduleSave();
        // An anchor the composer was docked in may be gone.
        normalizeComposer();
    }
    lap("composer");
    if (logTiming) {
        qCDebug(lcWellTiming).noquote() << "publish:" << stages.join(QStringLiteral(", ")) << "ms";
    }
    if (liveStart >= 0) {
        LiveEvents::span(QStringLiteral("workspace"), QStringLiteral("publish"), liveStart, LiveEvents::nowNs(), -1, stageMs);
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

QVariantMap WorkspaceStore::savedWorkspaceSummary(const QString &collectionId) const
{
    QVariantMap summary{{QStringLiteral("available"), false}};
    if (collectionId.isEmpty()) {
        return summary;
    }

    WorkspaceModel saved;
    const WorkspaceModel *model = &m_model;
    if (collectionId != m_collectionId) {
        const QJsonObject snapshot = QJsonDocument::fromJson(m_settings.value(snapshotKey(collectionId)).toString().toUtf8()).object();
        if (snapshot.isEmpty() || !Codec::decodeSnapshot(snapshot, m_registry, saved).ok) {
            return summary;
        }
        model = &saved;
    }

    QStringList titles;
    const AnchorRecord *focused = model->anchor(model->focusedAnchorId());
    const QString focusedViewId = focused ? focused->activeViewId : QString();
    if (const ViewRecord *view = model->view(focusedViewId); view && !view->title.trimmed().isEmpty()) {
        titles.append(view->title.trimmed());
    }
    const QStringList viewIds = model->viewIds();
    for (const QString &viewId : viewIds) {
        if (titles.size() >= 3) {
            break;
        }
        const ViewRecord *view = model->view(viewId);
        const QString title = view ? view->title.trimmed() : QString();
        if (viewId != focusedViewId && !title.isEmpty() && !titles.contains(title)) {
            titles.append(title);
        }
    }

    const QString layoutId = collectionId == m_collectionId
        ? m_currentLayoutId
        : m_settings.value(QStringLiteral("workspace/collections/%1/docking/layoutId").arg(collectionId)).toString();

    summary.insert(QStringLiteral("available"), true);
    summary.insert(QStringLiteral("viewCount"), static_cast<int>(viewIds.size()));
    summary.insert(QStringLiteral("titles"), titles);
    summary.insert(QStringLiteral("layoutName"), layoutName(layoutId));
    return summary;
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
    m_history.clear();
    m_redo.clear();
    m_previousPlaces.clear();
    loadArrangement();
    loadComposer();
    m_suggestion.clear();
    m_suggestionIntent.clear();
    m_recentAnchors.clear();
    emit agentChanged();
    emit historyChanged();
    m_currentLayoutId = collectionId.isEmpty()
        ? QString()
        : m_settings.value(QStringLiteral("workspace/collections/%1/docking/layoutId").arg(collectionId)).toString();
    emit layoutsChanged();
    emit collectionIdChanged();
    m_model.recoverSurfaces(currentScreens());
    publish();
    adoptOrphanSidebarSurfaces();
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

        // Put away in a Gravity Well: bring that one back instead of a copy.
        for (const QString &viewId : m_model.minimizedViewIds()) {
            const ViewRecord *minimized = m_model.view(viewId);
            if (minimized && minimized->typeId == typeId && minimized->resource.key() == resource.key()) {
                if (restoreMinimizedView(viewId).value(QStringLiteral("ok")).toBool()) {
                    if (!title.isEmpty()) {
                        m_model.setViewTitle(viewId, title);
                        publish();
                    }
                    return viewId;
                }
                break;
            }
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
    // Asked for while put away in a Well: it comes back.
    if (m_model.isMinimized(viewId)) {
        return restoreMinimizedView(viewId).value(QStringLiteral("ok")).toBool();
    }

    const OpResult result = m_model.activateView(viewId);

    if (!result.ok) {
        return false;
    }

    LiveEvents::Scope live(QStringLiteral("workspace"), QStringLiteral("switch-tab"));
    if (LiveEvents::recording()) {
        const ViewRecord *view = m_model.view(viewId);
        live.setField(QStringLiteral("viewType"), view ? view->typeId : QString());
    }
    const QString previous = m_model.focusedAnchorId();
    m_model.focusAnchor(m_model.anchorOfView(viewId));
    publish();
    tuckAwayFloatingPanels(previous);
    emit viewFocused(viewId);
    live.setOk(true);
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
        {QStringLiteral("minimized"), m_model.isMinimized(viewId)},
    };
}

QVariantMap WorkspaceStore::viewRow(const QString &viewId) const
{
    return m_views->row(viewId);
}

QStringList WorkspaceStore::viewsForResource(const QString &kind, const QString &id) const
{
    // Views in panels first, then any put away in a Well (activating one of
    // those restores it).
    QStringList views = m_model.viewsForResource(kind, id);
    for (const QString &viewId : m_model.minimizedViewIds()) {
        const ViewRecord *view = m_model.view(viewId);
        if (view && view->resource.kind == kind && view->resource.id == id) {
            views.append(viewId);
        }
    }
    return views;
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

    const QString previous = m_model.focusedAnchorId();
    const bool ok = check(m_model.focusAnchor(anchorId));
    if (ok) {
        tuckAwayFloatingPanels(previous);
    }
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
    // Hit-tested on the committed layout (as every dock zone is). What is
    // already shown (a join or a split preview) holds a little beyond its
    // zone, so the target doesn't flip while the pointer drifts.
    const Magnets::StripHit hit = Magnets::tabStripAt(
        m_model, surfaceId, QPointF(x, y), surfaceSize(surfaceId), m_metrics, stripHeight, {},
        m_drag.kind == DragSource::Kind::None ? DropTarget{} : m_dropTarget);
    if (hit.anchorId.isEmpty()) {
        return {};
    }
    return {
        {QStringLiteral("anchorId"), hit.anchorId},
        {QStringLiteral("localX"), hit.localX},
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
    if (resizing) {
        LiveEvents::end(m_liveResize);
        m_liveResize = LiveEvents::begin(QStringLiteral("workspace"), QStringLiteral("resize-split"));
    } else {
        LiveEvents::end(m_liveResize, 1);
        m_liveResize = 0;
        scheduleSave();
    }
}

// ---- drags ---------------------------------------------------------------

bool WorkspaceStore::pointerButtonDown() const
{
    return QGuiApplication::mouseButtons() != Qt::NoButton;
}

bool WorkspaceStore::beginDrag(const QString &kind, const QString &id)
{
    // One drag at a time: something else under the pointer can't start a
    // second one while a magnet is carried.
    if (m_drag.kind != DragSource::Kind::None) {
        return false;
    }

    DragSource source;

    if (kind == QStringLiteral("view") && m_model.view(id)) {
        source = DragSource::view(id);
    } else if (kind == QStringLiteral("anchor") && m_model.anchor(id)) {
        source = DragSource::anchor(id);
    } else {
        return false;
    }

    m_drag = source;
    LiveEvents::end(m_liveDrag);
    m_liveDrag = LiveEvents::begin(QStringLiteral("workspace"), QStringLiteral("drag"), {{QStringLiteral("kind"), kind}});
    m_dropTarget = {};
    m_dropAllowed = false;
    m_landingRect = {};
    m_preview = {};
    m_previewSurfaceId.clear();
    m_dropRefusal = {};
    m_landingAnchorId.clear();
    // Where it came from: drawn under the pickup, and where Return puts it.
    m_dragOrigin = Magnets::placeOf(m_model, source);
    const QString sourceAnchor = source.kind == DragSource::Kind::View ? m_model.anchorOfView(source.id) : source.id;
    m_dragSourceRect = m_model.computeGeometry(m_dragOrigin.surfaceId, surfaceSize(m_dragOrigin.surfaceId), m_metrics)
                           .anchors.value(sourceAnchor);
    m_dragTargetRevision = m_model.revision();
    m_dragPhase = Magnets::Phase::Pickup;
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
    // target does not jump as panels ease aside. Only the dock zones count:
    // anywhere else the drop floats.
    leaveSlot();
    if (m_dragDetached) {
        m_dragDetached = false;
        emit dragChanged();
    }
    // The zone already chosen holds while the pointer stays near it.
    const DropTarget held = m_dropTarget.isValid() ? m_dropTarget : m_aimTarget;
    aimAt(Magnets::dockTargetAt(m_model, surfaceId, QPointF(x, y), surfaceSize(surfaceId), m_metrics, {}, held));
}

void WorkspaceStore::aimAt(const DropTarget &target)
{
    const auto same = [](const DropTarget &a, const DropTarget &b) {
        return a.surfaceId == b.surfaceId && a.anchorId == b.anchorId && a.edge == b.edge && a.index == b.index;
    };
    if (!target.isValid()) {
        stopAiming();
        setDropTarget({});
        return;
    }
    if (same(target, m_dropTarget)) {
        stopAiming(); // already docking there
        return;
    }
    if (m_dockDwellMs <= 0) {
        stopAiming();
        setDropTarget(target);
        return;
    }
    if (same(target, m_aimTarget)) {
        return; // still holding: the dwell runs on
    }
    // A new zone: let go of any other, and dock here only if held.
    m_aimTarget = target;
    ++m_aimSerial;
    m_aimTimer.start(m_dockDwellMs);
    setDropTarget({});
    m_dragPhase = Magnets::Phase::Aiming;
    emit dragChanged();
}

void WorkspaceStore::stopAiming()
{
    m_aimTimer.stop();
    if (m_aimTarget.isValid()) {
        m_aimTarget = {};
        if (m_dragPhase == Magnets::Phase::Aiming) {
            m_dragPhase = Magnets::Phase::Free;
        }
        emit dragChanged();
    }
}

void WorkspaceStore::setDockDwellMs(int ms)
{
    ms = std::max(0, ms);
    if (ms != m_dockDwellMs) {
        m_dockDwellMs = ms;
        emit dragChanged();
    }
}

bool WorkspaceStore::floatAllowed()
{
    if (m_floatAllowed < 0 && m_drag.kind != DragSource::Kind::None) {
        m_floatRefusal = Magnets::canFloat(m_model, m_drag);
        m_floatAllowed = m_floatRefusal.ok ? 1 : 0;
    }
    return m_floatAllowed == 1;
}

void WorkspaceStore::leaveSlot()
{
    if (!m_slotTarget.isEmpty() || !m_wellTarget.isEmpty()) {
        m_slotTarget.clear();
        m_wellTarget.clear();
        m_dropAllowed = false;
        m_dragPhase = Magnets::Phase::Free;
        emit dragChanged();
    }
}

void WorkspaceStore::updateDragOverSlot(const QString &slot)
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }
    if (slot == m_slotTarget) {
        return;
    }
    stopAiming();
    if (m_dragDetached) {
        m_dragDetached = false;
    }
    setDropTarget({});
    m_wellTarget.clear();
    m_slotTarget = slot;
    const QString holder = m_model.surfaceInSidebarSlot(slot);
    const QString sourceAnchor = m_drag.kind == DragSource::Kind::View ? m_model.anchorOfView(m_drag.id) : m_drag.id;
    const bool own = !holder.isEmpty() && holder == m_model.surfaceOfAnchor(sourceAnchor);
    const bool free = holder.isEmpty();
    m_dropAllowed = free && floatAllowed();
    m_dropRefusal = own ? OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."))
        : !free ? OpResult::failure(QStringLiteral("SLOT_TAKEN"), QStringLiteral("Something is already docked there."))
                : m_floatRefusal;
    m_dragPhase = m_dropAllowed ? Magnets::Phase::Preview : Magnets::Phase::Invalid;
    ++m_dragPreviewSerial;
    emit dragChanged();
}

void WorkspaceStore::updateDragOverWell(const QString &wellId)
{
    if (m_drag.kind == DragSource::Kind::None || wellId == m_wellTarget) {
        return;
    }
    if (wellId.isEmpty()) {
        leaveSlot();
        return;
    }
    stopAiming();
    m_dragDetached = false;
    setDropTarget({});
    m_slotTarget.clear();
    m_wellTarget = wellId;
    // A panel locked in place can't go; anything else can.
    const QString sourceAnchor = m_drag.kind == DragSource::Kind::View ? m_model.anchorOfView(m_drag.id) : m_drag.id;
    const AnchorRecord *anchor = m_model.anchor(sourceAnchor);
    const bool locked = m_drag.kind == DragSource::Kind::Anchor && anchor && anchor->layoutLocked;
    m_dropAllowed = !locked && m_model.well(wellId);
    m_dropRefusal = m_dropAllowed ? OpResult::success()
                                  : OpResult::failure(QStringLiteral("LAYOUT_LOCKED"), QStringLiteral("It's locked in place."));
    m_dragPhase = m_dropAllowed ? Magnets::Phase::Preview : Magnets::Phase::Invalid;
    ++m_dragPreviewSerial;
    emit dragChanged();
}

bool WorkspaceStore::dropFloats() const
{
    return m_drag.kind != DragSource::Kind::None && !m_dropTarget.isValid() && !m_dragDetached && m_slotTarget.isEmpty()
        && m_wellTarget.isEmpty()
        && m_floatRect.isValid() && m_floatAllowed == 1;
}

void WorkspaceStore::updateDragFloat(double x, double y, double width, double height)
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }
    const QRectF rect(x, y, std::max(240.0, width), std::max(160.0, height));
    const bool known = m_floatAllowed >= 0;
    floatAllowed();
    if (m_floatRect == rect && known) {
        return;
    }
    m_floatRect = rect;
    emit dragChanged();
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
    // A tab strip is aimed at on purpose: no dwell.
    stopAiming();
    leaveSlot();
    setDropTarget(target);
}

void WorkspaceStore::clearDragTarget()
{
    stopAiming();
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
    stopAiming();
    leaveSlot();

    if (m_dragDetached && m_detachRect == rect && m_detachScreen == screenName && m_dropAllowed == allowed) {
        return;
    }

    m_dropRefusal = allowed ? OpResult::success()
                            : OpResult::failure(QStringLiteral("ALREADY_ALONE"),
                                                QStringLiteral("It's already alone in its own window: move the window instead."));
    m_landingAnchorId.clear();
    m_dragPhase = allowed ? Magnets::Phase::Detached : Magnets::Phase::Invalid;
    ++m_dragPreviewSerial;

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
        // First move over nothing: picked up becomes free dragging.
        if (!target.isValid() && m_dragPhase == Magnets::Phase::Pickup) {
            m_dragPhase = Magnets::Phase::Free;
            emit dragChanged();
        }
        return;
    }

    m_dragDetached = false;
    m_dropTarget = target;
    m_dropAllowed = false;
    m_dropRefusal = {};
    m_landingRect = {};
    m_landingAnchorId.clear();
    m_preview = {};
    m_previewSurfaceId.clear();
    m_dragTargetRevision = m_model.revision();
    ++m_dragPreviewSerial;

    if (target.isValid()) {
        // Worked out on a copy of the layout: nothing is committed.
        const Magnets::Preview preview = Magnets::preview(m_model, m_drag, target, surfaceSize(target.surfaceId), m_metrics);
        m_dropAllowed = preview.result.ok;
        m_dropRefusal = preview.result;
        if (m_dropAllowed) {
            m_preview = preview.geometry;
            m_previewSurfaceId = target.surfaceId;
            m_landingRect = preview.landingRect;
            m_landingAnchorId = preview.landingAnchorId;
        }
        m_dragPhase = m_dropAllowed ? Magnets::Phase::Preview : Magnets::Phase::Invalid;
    } else if (m_drag.kind != DragSource::Kind::None) {
        m_dragPhase = Magnets::Phase::Free;
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
    m_dropRefusal = {};
    m_landingAnchorId.clear();
    m_dragOrigin = {};
    m_dragSourceRect = {};
    m_dragTargetRevision = -1;
    m_dragPhase = Magnets::Phase::Idle;
    m_floatRect = {};
    m_floatAllowed = -1;
    m_floatRefusal = {};
    m_aimTimer.stop();
    m_aimTarget = {};
    m_slotTarget.clear();
    m_wellTarget.clear();
}

bool WorkspaceStore::finishDrag()
{
    if (m_drag.kind == DragSource::Kind::None) {
        return false;
    }

    const DragSource source = m_drag;
    const DropTarget target = m_dropTarget;
    const QString previousFocus = m_model.focusedAnchorId();
    bool allowed = m_dropAllowed;
    OpResult refusal = m_dropRefusal;
    const bool detached = m_dragDetached;
    const QRectF detachRect = m_detachRect;
    const QString detachScreen = m_detachScreen;
    const Magnets::Place origin = m_dragOrigin;
    // Over a Well: it's put away there.
    const QString well = m_wellTarget;
    if (!well.isEmpty()) {
        const bool wellAllowed = m_dropAllowed;
        resetDrag();
        return dropIntoWell(source, well, wellAllowed);
    }
    // Over an empty sidebar slot: it docks there.
    const QString slot = m_slotTarget;
    // No dock target: it floats as a panel where it was let go.
    const bool floating = !detached && !target.isValid() && slot.isEmpty() && m_floatRect.isValid();
    const QRectF floatRect = m_floatRect;
    if (floating) {
        allowed = floatAllowed();
        refusal = m_floatRefusal;
    }

    // The layout changed under the drag (an agent, a window closing):
    // judge the target again against the layout as it is now.
    if (allowed && !detached && !floating && slot.isEmpty() && m_model.revision() != m_dragTargetRevision) {
        const Magnets::Preview fresh = Magnets::preview(m_model, source, target, surfaceSize(target.surfaceId), m_metrics);
        allowed = fresh.result.ok;
        refusal = fresh.result;
    }

    resetDrag();

    QVariantMap details{
        {QStringLiteral("kind"), source.kind == DragSource::Kind::View ? QStringLiteral("view") : QStringLiteral("anchor")},
        {QStringLiteral("id"), source.id},
        {QStringLiteral("target"), detached ? QVariantMap{{QStringLiteral("detached"), true}}
                                   : !slot.isEmpty() ? QVariantMap{{QStringLiteral("sidebarSlot"), slot}}
                                   : floating ? QVariantMap{{QStringLiteral("floating"), true}, {QStringLiteral("rect"), Magnets::rectMap(floatRect)}}
                                              : Magnets::targetMap(target)},
    };

    bool ok = false;
    // One drop, one workspace step.
    HistoryStep step = historyStep(detached
        ? QStringLiteral("Opened %1 in a new window").arg(dragSourceTitle(source))
        : !slot.isEmpty() ? QStringLiteral("Docked %1 in the sidebar").arg(dragSourceTitle(source))
        : floating && !origin.aloneInFloatingWindow ? QStringLiteral("Floated %1").arg(dragSourceTitle(source))
        : QStringLiteral("Moved %1").arg(dragSourceTitle(source)));

    if (allowed && !slot.isEmpty()) {
        // The sidebar keeps a size for it (the bottom slot's height).
        const QRectF frame(0, 0, 280, 260);
        OpResult result = m_model.surfaceInSidebarSlot(slot).isEmpty()
            ? (source.kind == DragSource::Kind::View
                   ? m_model.detachView(source.id, frame, {}, Origin::User, false)
                   : m_model.detachAnchor(source.id, frame, {}, false))
            : OpResult::failure(QStringLiteral("SLOT_TAKEN"), QStringLiteral("Something is already docked there."));
        if (result.ok) {
            result = m_model.setSurfaceSidebarSlot(result.createdId, slot);
        }
        ok = result.ok;
        if (ok) {
            const QString anchorId = source.kind == DragSource::Kind::View ? m_model.anchorOfView(source.id) : source.id;
            if (source.kind == DragSource::Kind::View) {
                m_model.activateView(source.id);
            }
            m_model.focusAnchor(anchorId);
            details.insert(QStringLiteral("landingAnchorId"), anchorId);
            details.insert(QStringLiteral("surfaceId"), result.createdId);
        } else {
            refusal = result;
            emit operationFailed(result.message);
        }
    } else if (allowed && floating) {
        const OpResult result = source.kind == DragSource::Kind::View
            ? m_model.detachView(source.id, floatRect, {}, Origin::User, true)
            : m_model.detachAnchor(source.id, floatRect, {}, true);
        ok = result.ok;
        if (ok) {
            const QString anchorId = source.kind == DragSource::Kind::View ? m_model.anchorOfView(source.id) : source.id;
            if (source.kind == DragSource::Kind::View) {
                m_model.activateView(source.id);
            }
            m_model.focusAnchor(anchorId);
            details.insert(QStringLiteral("landingAnchorId"), anchorId);
            details.insert(QStringLiteral("surfaceId"), result.createdId);
        } else {
            refusal = result;
            emit operationFailed(result.message);
        }
    } else if (allowed && detached) {
        const OpResult result = source.kind == DragSource::Kind::View
            ? m_model.detachView(source.id, detachRect, detachScreen)
            : m_model.detachAnchor(source.id, detachRect, detachScreen);
        ok = result.ok;
        if (!ok) {
            refusal = result;
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
            details.insert(QStringLiteral("landingAnchorId"), anchorId);
        } else {
            refusal = result;
            emit operationFailed(result.message);
        }
    }

    if (ok) {
        recordStep(std::move(step));
        m_previousPlaces.insert(dragKey(source), origin);
    }

    publish();
    emit dragChanged();
    if (ok) {
        tuckAwayFloatingPanels(previousFocus);
        emit dragFinished(QStringLiteral("committed"), details);
    } else if (target.isValid() || detached || floating || !slot.isEmpty()) {
        details.insert(QStringLiteral("code"), refusal.code);
        details.insert(QStringLiteral("reason"), refusal.message);
        emit dragFinished(QStringLiteral("refused"), details);
    } else {
        emit dragFinished(QStringLiteral("cancelled"), details);
    }
    return ok;
}

void WorkspaceStore::cancelDrag()
{
    if (m_drag.kind == DragSource::Kind::None) {
        return;
    }

    const QVariantMap details{
        {QStringLiteral("kind"), dragKind()},
        {QStringLiteral("id"), m_drag.id},
    };
    resetDrag();
    publish();
    emit dragChanged();
    emit dragFinished(QStringLiteral("cancelled"), details);
}

// ---- surfaces ------------------------------------------------------------

QStringList WorkspaceStore::floatingSurfaceIds() const
{
    QStringList ids;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        const SurfaceRecord *surface = m_model.surface(surfaceId);
        if (surface && !surface->primary && !surface->inMainWindow && surface->sidebarSlot.isEmpty()) {
            ids.append(surfaceId);
        }
    }
    return ids;
}

QStringList WorkspaceStore::panelSurfaceIds() const
{
    QStringList ids;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        const SurfaceRecord *surface = m_model.surface(surfaceId);
        if (surface && surface->inMainWindow && surface->sidebarSlot.isEmpty()) {
            ids.append(surfaceId);
        }
    }
    return ids;
}

QStringList WorkspaceStore::sidebarSurfaceIds() const
{
    QStringList ids;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        const SurfaceRecord *surface = m_model.surface(surfaceId);
        if (surface && !surface->sidebarSlot.isEmpty()) {
            ids.append(surfaceId);
        }
    }
    return ids;
}

QString WorkspaceStore::sidebarSurface(const QString &slot) const
{
    return m_model.surfaceInSidebarSlot(slot);
}

void WorkspaceStore::setSidebarSlots(const QStringList &slotIds)
{
    m_sidebarSlots = slotIds;
    m_sidebarSlotsKnown = true;
    // A tab deleted while a drag is over it: the drag lets go of it.
    if (!m_slotTarget.isEmpty() && m_slotTarget != QStringLiteral("sidebar.bottom") && !slotIds.contains(m_slotTarget)) {
        leaveSlot();
    }
    adoptOrphanSidebarSurfaces();
}

void WorkspaceStore::adoptOrphanSidebarSurfaces()
{
    if (!m_sidebarSlotsKnown) {
        return;
    }
    bool changed = false;
    int offset = 0;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        const SurfaceRecord *surface = m_model.surface(surfaceId);
        if (!surface || surface->sidebarSlot.isEmpty() || surface->sidebarSlot == QStringLiteral("sidebar.bottom")
            || m_sidebarSlots.contains(surface->sidebarSlot)) {
            continue;
        }
        // Its tab is gone: it floats, a little apart from any others.
        m_model.setSurfaceSidebarSlot(surfaceId, {});
        m_model.setSurfaceGeometry(surfaceId, QRectF(120 + offset, 100 + offset, 520, 360), {}, true);
        offset += 28;
        changed = true;
    }
    if (changed) {
        publish();
    }
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
        {QStringLiteral("inMainWindow"), record->inMainWindow},
        {QStringLiteral("sidebarSlot"), record->sidebarSlot},
        {QStringLiteral("leaveOnTop"), record->leaveOnTop},
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
    const bool inMainWindow = record->inMainWindow;
    if (inMainWindow) {
        windowMaximized = false;
    }
    if (record->windowMaximized != windowMaximized) {
        m_model.setSurfaceWindowMaximized(surfaceId, windowMaximized);
    }
    const QString screen = inMainWindow ? QString() : screenName;
    bool geometryChanged = false;
    if (!windowMaximized && geometry.isValid() && (record->geometry != geometry || record->screenName != screen)) {
        m_model.setSurfaceGeometry(surfaceId, geometry, screen, true);
        geometryChanged = true;
    }

    if (m_publishedModelRevision != m_model.revision()) {
        m_publishedModelRevision = m_model.revision();
        scheduleSave();
    }
    if (inMainWindow && geometryChanged) {
        ++m_revision;
        emit revisionChanged();
    }
    return true;
}

QString WorkspaceStore::detachView(const QString &viewId, double x, double y, double width, double height, const QString &screenName)
{
    LiveEvents::Scope live(QStringLiteral("workspace"), QStringLiteral("undock"), {{QStringLiteral("kind"), QStringLiteral("view")}, {QStringLiteral("how"), QStringLiteral("window")}});
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const OpResult result = m_model.detachView(viewId, QRectF(x, y, width, height), screenName);
    live.setOk(result.ok);
    return check(result) ? result.createdId : QString();
}

QString WorkspaceStore::detachAnchor(const QString &anchorId, double x, double y, double width, double height, const QString &screenName)
{
    LiveEvents::Scope live(QStringLiteral("workspace"), QStringLiteral("undock"), {{QStringLiteral("kind"), QStringLiteral("anchor")}, {QStringLiteral("how"), QStringLiteral("window")}});
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const OpResult result = m_model.detachAnchor(anchorId, QRectF(x, y, width, height), screenName);
    live.setOk(result.ok);
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
    return layoutName(m_currentLayoutId);
}

QString WorkspaceStore::layoutName(const QString &layoutId) const
{
    if (layoutId.isEmpty()) {
        return {};
    }

    for (const Presets::Preset &preset : Presets::all()) {
        if (preset.id == layoutId) {
            return preset.name;
        }
    }

    for (const QJsonValue &value : readSavedLayouts()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == layoutId) {
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

    HistoryStep step = historyStep(QStringLiteral("Switched layouts"));
    const Codec::LoadReport report = Codec::applyLayout(layout, m_model);

    if (!report.ok) {
        emit operationFailed(report.error);
        return false;
    }

    setCurrentLayoutId(layoutId);
    step.summary = QStringLiteral("Switched to the “%1” layout").arg(currentLayoutName());
    recordStep(step);
    publish();
    return true;
}

bool WorkspaceStore::applyLayout(const QString &layoutId)
{
    LiveEvents::Scope live(QStringLiteral("workspace"), QStringLiteral("change-layout"));
    live.setOk(false);
    const QJsonObject layout = layoutDocument(layoutId);

    if (layout.isEmpty()) {
        emit operationFailed(QStringLiteral("Unknown layout."));
        return false;
    }

    if (!applyLayoutDocument(layout, layoutId)) {
        return false;
    }

    emit layoutApplied(layoutId, currentLayoutName());
    live.setOk(true);
    return true;
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
