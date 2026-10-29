// Magnets in the store: the published drag state and Return to previous
// position. The drag itself (begin, update, finish, cancel) lives with the
// other drag code in workspace_store.cpp; what a drop would do is decided
// in Core (workspace_magnets).

#include "App/Domains/Workspace/workspace_store.h"

using namespace Workspace;

QString WorkspaceStore::dragKey(const DragSource &source)
{
    return (source.kind == DragSource::Kind::View ? QStringLiteral("view:") : QStringLiteral("anchor:")) + source.id;
}

DragSource WorkspaceStore::dragSourceFor(const QString &kind, const QString &id) const
{
    if (kind == QStringLiteral("view") && m_model.view(id)) {
        return DragSource::view(id);
    }
    if (kind == QStringLiteral("anchor") && m_model.anchor(id)) {
        return DragSource::anchor(id);
    }
    return {};
}

QString WorkspaceStore::dragSourceTitle(const DragSource &source) const
{
    if (source.kind == DragSource::Kind::View) {
        const ViewRecord *view = m_model.view(source.id);
        return view && !view->title.isEmpty() ? QStringLiteral("“%1”").arg(view->title) : QStringLiteral("a tab");
    }
    const QString name = m_model.anchorDisplayName(source.id);
    return name.isEmpty() ? QStringLiteral("a panel") : QStringLiteral("“%1”").arg(name);
}

// How each panel in the target window moves if the drop lands: from its
// committed rect to its proposed one. A panel the drop creates has no
// "from" (its id is provisional until the drop); one it empties has no "to".
QVariantList WorkspaceStore::dragReflow() const
{
    QVariantList list;
    if (m_previewSurfaceId.isEmpty()) {
        return list;
    }
    const LayoutGeometry committed = m_model.computeGeometry(m_previewSurfaceId, surfaceSize(m_previewSurfaceId), m_metrics);
    QStringList ids = committed.anchors.keys();
    for (auto it = m_preview.anchors.cbegin(); it != m_preview.anchors.cend(); ++it) {
        if (!ids.contains(it.key())) {
            ids.append(it.key());
        }
    }
    ids.sort();
    for (const QString &anchorId : std::as_const(ids)) {
        const QRectF from = committed.anchors.value(anchorId);
        const QRectF to = m_preview.anchors.value(anchorId);
        if (from == to) {
            continue;
        }
        list.append(QVariantMap{
            {QStringLiteral("anchorId"), anchorId},
            {QStringLiteral("from"), Magnets::rectMap(from)},
            {QStringLiteral("to"), Magnets::rectMap(to)},
            {QStringLiteral("entering"), from.isNull()},
            {QStringLiteral("leaving"), to.isNull()},
        });
    }
    return list;
}

Workspace::OpResult WorkspaceStore::currentRefusal() const
{
    if (m_drag.kind != DragSource::Kind::None && !m_dropTarget.isValid() && !m_dragDetached && m_floatAllowed == 0) {
        return m_floatRefusal;
    }
    return m_dropRefusal;
}

QVariantMap WorkspaceStore::dragState() const
{
    if (m_drag.kind == DragSource::Kind::None) {
        return {{QStringLiteral("phase"), Magnets::phaseName(Magnets::Phase::Idle)}, {QStringLiteral("active"), false}};
    }
    return {
        {QStringLiteral("phase"), Magnets::phaseName(
            m_dragPhase == Magnets::Phase::Free && m_floatAllowed == 0 ? Magnets::Phase::Invalid : m_dragPhase)},
        {QStringLiteral("active"), true},
        {QStringLiteral("kind"), dragKind()},
        {QStringLiteral("id"), m_drag.id},
        {QStringLiteral("title"), dragSourceTitle(m_drag)},
        {QStringLiteral("origin"), QVariantMap{
            {QStringLiteral("surfaceId"), m_dragOrigin.surfaceId},
            {QStringLiteral("anchorId"), m_drag.kind == DragSource::Kind::View ? m_dragOrigin.anchorId : m_drag.id},
            {QStringLiteral("index"), m_dragOrigin.index},
            {QStringLiteral("rect"), Magnets::rectMap(m_dragSourceRect)},
        }},
        {QStringLiteral("target"), Magnets::targetMap(m_dropTarget)},
        {QStringLiteral("valid"), m_dropAllowed || dropFloats()},
        {QStringLiteral("reasonCode"), currentRefusal().ok ? QString() : currentRefusal().code},
        {QStringLiteral("reason"), dropRefusal()},
        {QStringLiteral("landingAnchorId"), m_landingAnchorId},
        {QStringLiteral("landingRect"), m_dropAllowed ? Magnets::rectMap(m_landingRect) : QVariantMap()},
        {QStringLiteral("detachRect"), detachRect()},
        // No dock target: a drop floats it here (main window coordinates).
        {QStringLiteral("floats"), dropFloats()},
        {QStringLiteral("floatRect"), Magnets::rectMap(m_floatRect)},
        // Held at a dock zone: it docks there after `dwellMs` (PR 2 can
        // draw the pull from this).
        {QStringLiteral("aim"), Magnets::targetMap(m_aimTarget)},
        // Over an empty sidebar slot (a drop docks it there).
        {QStringLiteral("slot"), m_slotTarget},
        {QStringLiteral("aimSerial"), m_aimSerial},
        {QStringLiteral("dwellMs"), m_dockDwellMs},
        {QStringLiteral("reflow"), dragReflow()},
        // Bumps on every target change: "preview updated".
        {QStringLiteral("previewSerial"), m_dragPreviewSerial},
        {QStringLiteral("layoutRevision"), m_model.revision()},
    };
}

bool WorkspaceStore::canReturnToPrevious(const QString &kind, const QString &id) const
{
    const DragSource source = dragSourceFor(kind, id);
    const auto it = m_previousPlaces.constFind(dragKey(source));
    return source.kind != DragSource::Kind::None && it != m_previousPlaces.cend()
        && Magnets::planReturn(m_model, *it).result.ok;
}

QVariantMap WorkspaceStore::returnToPrevious(const QString &kind, const QString &id)
{
    const auto failure = [](const OpResult &result) {
        return QVariantMap{
            {QStringLiteral("ok"), false},
            {QStringLiteral("code"), result.code},
            {QStringLiteral("error"), result.message},
        };
    };

    const DragSource source = dragSourceFor(kind, id);
    if (source.kind == DragSource::Kind::None) {
        return failure(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open.")));
    }
    const auto it = m_previousPlaces.constFind(dragKey(source));
    if (it == m_previousPlaces.cend()) {
        return failure(OpResult::failure(QStringLiteral("NO_PLACE"), QStringLiteral("It hasn't been moved yet.")));
    }
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }

    const Magnets::ReturnPlan plan = Magnets::planReturn(m_model, *it);
    if (!plan.result.ok) {
        return failure(plan.result);
    }

    // Returning again goes back to here.
    const Magnets::Place here = Magnets::placeOf(m_model, source);
    HistoryStep step = historyStep(QStringLiteral("Put %1 back").arg(dragSourceTitle(source)));
    const OpResult result = plan.detach
        ? (source.kind == DragSource::Kind::View
               ? m_model.detachView(source.id, plan.geometry, plan.screenName, Origin::User, plan.inMainWindow)
               : m_model.detachAnchor(source.id, plan.geometry, plan.screenName, plan.inMainWindow))
        : m_model.applyDrop(source, plan.target);
    if (!result.ok) {
        return failure(result);
    }

    const QString anchorId = source.kind == DragSource::Kind::View ? m_model.anchorOfView(source.id) : source.id;
    if (plan.detach && !plan.sidebarSlot.isEmpty()) {
        m_model.setSurfaceSidebarSlot(m_model.surfaceOfAnchor(anchorId), plan.sidebarSlot);
    }
    if (!plan.detach && plan.target.edge != Edge::Center) {
        // Recreated beside its neighbour: the same size as before.
        Magnets::restoreShare(m_model, anchorId, *it);
    }
    if (source.kind == DragSource::Kind::View) {
        m_model.activateView(source.id);
    }
    m_model.focusAnchor(anchorId);
    recordStep(std::move(step));
    m_previousPlaces.insert(dragKey(source), here);
    publish();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("anchorId"), anchorId}};
}

QString WorkspaceStore::floatView(const QString &viewId, double x, double y, double width, double height)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const DragSource source = dragSourceFor(QStringLiteral("view"), viewId);
    if (source.kind == DragSource::Kind::None) {
        return {};
    }
    const Magnets::Place here = Magnets::placeOf(m_model, source);
    HistoryStep step = historyStep(QStringLiteral("Floated %1").arg(dragSourceTitle(source)));
    const OpResult result = m_model.detachView(viewId, QRectF(x, y, width, height), {}, Origin::User, true);
    if (!result.ok) {
        emit operationFailed(result.message.isEmpty() ? result.code : result.message);
        return {};
    }
    m_model.activateView(viewId);
    m_model.focusAnchor(m_model.anchorOfView(viewId));
    recordStep(std::move(step));
    m_previousPlaces.insert(dragKey(source), here);
    publish();
    return result.createdId;
}

QString WorkspaceStore::floatAnchor(const QString &anchorId, double x, double y, double width, double height)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const DragSource source = dragSourceFor(QStringLiteral("anchor"), anchorId);
    if (source.kind == DragSource::Kind::None) {
        return {};
    }
    const Magnets::Place here = Magnets::placeOf(m_model, source);
    HistoryStep step = historyStep(QStringLiteral("Floated %1").arg(dragSourceTitle(source)));
    const OpResult result = m_model.detachAnchor(anchorId, QRectF(x, y, width, height), {}, true);
    if (!result.ok) {
        emit operationFailed(result.message.isEmpty() ? result.code : result.message);
        return {};
    }
    m_model.focusAnchor(anchorId);
    recordStep(std::move(step));
    m_previousPlaces.insert(dragKey(source), here);
    publish();
    return result.createdId;
}
