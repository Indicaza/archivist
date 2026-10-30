// Gravity Wells in the store: minimize, restore, pin and move, each one
// workspace step (undo puts it back), published like any other change. What
// a restore does is decided in Core (workspace_wells).

#include "App/Domains/Workspace/workspace_store.h"

using namespace Workspace;

namespace
{
QVariantMap failed(const OpResult &result)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("code"), result.code},
        {QStringLiteral("error"), result.message},
    };
}
} // namespace

QVariantList WorkspaceStore::wells() const
{
    return Wells::wellsList(m_model);
}

bool WorkspaceStore::isViewMinimized(const QString &viewId) const
{
    return m_model.isMinimized(viewId);
}

bool WorkspaceStore::isViewPinned(const QString &viewId) const
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return false;
    }
    const WellEntry *entry = m_model.wellEntry(m_model.wellEntryForResource(WorkspaceModel::kMainWellId, view->resource));
    return entry && entry->pinned;
}

QVariantMap WorkspaceStore::wellResult(const Wells::Restored &restored) const
{
    if (!restored.result.ok) {
        return failed(restored.result);
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("viewId"), restored.viewId},
        {QStringLiteral("anchorId"), restored.anchorId},
        {QStringLiteral("how"), restored.how},
    };
}

QVariantMap WorkspaceStore::minimizeView(const QString &viewId)
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return failed(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open.")));
    }
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const QString title = view->title.isEmpty() ? QStringLiteral("a tab") : QStringLiteral("“%1”").arg(view->title);
    HistoryStep step = historyStep(QStringLiteral("Minimized %1").arg(title));
    QElapsedTimer clock;
    clock.start();
    const OpResult result = Wells::minimize(m_model, viewId);
    if (!result.ok) {
        return failed(result);
    }
    recordStep(std::move(step));
    const qint64 model = clock.nsecsElapsed();
    publish();
    qCDebug(lcWellTiming).noquote() << "minimize" << title << "model" << model / 1e6 << "ms, publish" << (clock.nsecsElapsed() - model) / 1e6 << "ms";
    return {{QStringLiteral("ok"), true}, {QStringLiteral("entryId"), result.createdId}};
}

QVariantMap WorkspaceStore::minimizeAnchor(const QString &anchorId)
{
    if (!m_model.anchor(anchorId)) {
        return failed(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("That panel is gone.")));
    }
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    HistoryStep step = historyStep(QStringLiteral("Minimized “%1”").arg(m_model.anchorDisplayName(anchorId)));
    const OpResult result = Wells::minimizeAnchor(m_model, anchorId);
    if (!result.ok) {
        return failed(result);
    }
    recordStep(std::move(step));
    publish();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("entryId"), result.createdId}};
}

bool WorkspaceStore::setSurfaceLeaveOnTop(const QString &surfaceId, bool leaveOnTop)
{
    const int before = m_model.revision();
    if (!m_model.setSurfaceLeaveOnTop(surfaceId, leaveOnTop).ok) {
        return false;
    }
    if (m_model.revision() != before) {
        publish();
    }
    return true;
}

bool WorkspaceStore::tucksAway(const QString &surfaceId) const
{
    const SurfaceRecord *surface = m_model.surface(surfaceId);
    return surface && !surface->primary && surface->inMainWindow && surface->sidebarSlot.isEmpty() && !surface->leaveOnTop;
}

void WorkspaceStore::tuckAwayFloatingPanels(const QString &previousFocus)
{
    const QString surfaceId = m_model.surfaceOfAnchor(previousFocus);
    if (!m_tuckFloatingPanels || m_drag.kind != DragSource::Kind::None || !tucksAway(surfaceId)
        || m_model.surfaceOfAnchor(m_model.focusedAnchorId()) == surfaceId) {
        return;
    }
    const QStringList anchors = m_model.anchorIds(surfaceId);
    QStringList names;
    for (const QString &anchorId : anchors) {
        names.append(m_model.anchorDisplayName(anchorId));
    }
    HistoryStep step = historyStep(QStringLiteral("Tucked “%1” into the Well").arg(names.join(QStringLiteral(", "))));
    bool changed = false;
    for (const QString &anchorId : anchors) {
        changed = Wells::minimizeAnchor(m_model, anchorId).ok || changed;
    }
    if (changed) {
        recordStep(std::move(step));
        publish();
    }
}

bool WorkspaceStore::dropIntoWell(const DragSource &source, const QString &wellId, bool allowed)
{
    QVariantMap details{
        {QStringLiteral("kind"), source.kind == DragSource::Kind::View ? QStringLiteral("view") : QStringLiteral("anchor")},
        {QStringLiteral("id"), source.id},
        {QStringLiteral("target"), QVariantMap{{QStringLiteral("well"), wellId}}},
    };
    OpResult result = allowed ? OpResult::success()
                              : OpResult::failure(QStringLiteral("LAYOUT_LOCKED"), QStringLiteral("It's locked in place."));
    if (result.ok) {
        HistoryStep step = historyStep(QStringLiteral("Minimized %1").arg(dragSourceTitle(source)));
        result = source.kind == DragSource::Kind::View ? Wells::minimize(m_model, source.id, wellId)
                                                       : Wells::minimizeAnchor(m_model, source.id, wellId);
        if (result.ok) {
            recordStep(std::move(step));
            details.insert(QStringLiteral("entryId"), result.createdId);
        }
    }
    publish();
    emit dragChanged();
    if (result.ok) {
        emit dragFinished(QStringLiteral("committed"), details);
    } else {
        details.insert(QStringLiteral("code"), result.code);
        details.insert(QStringLiteral("reason"), result.message);
        emit operationFailed(result.message);
        emit dragFinished(QStringLiteral("refused"), details);
    }
    return result.ok;
}

QVariantMap WorkspaceStore::restoreMinimizedView(const QString &viewId)
{
    if (!m_model.isMinimized(viewId)) {
        return failed(OpResult::failure(QStringLiteral("NOT_MINIMIZED"), QStringLiteral("It isn't put away.")));
    }
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const ViewRecord *view = m_model.view(viewId);
    const WellEntry *entry = m_model.wellEntry(m_model.entryOfMinimizedView(viewId));
    const QString name = entry && entry->panel ? entry->title : view->title;
    const QString title = name.isEmpty() ? QStringLiteral("a tab") : QStringLiteral("“%1”").arg(name);
    HistoryStep step = historyStep(QStringLiteral("Restored %1").arg(title));
    const QString previous = m_model.focusedAnchorId();
    QElapsedTimer clock;
    clock.start();
    const Wells::Restored restored = Wells::restore(m_model, viewId);
    if (!restored.result.ok) {
        return failed(restored.result);
    }
    recordStep(std::move(step));
    const qint64 model = clock.nsecsElapsed();
    publish();
    const qint64 published = clock.nsecsElapsed();
    // One floating panel out at a time: the one it replaces goes back.
    tuckAwayFloatingPanels(previous);
    emit viewFocused(viewId);
    qCDebug(lcWellTiming).noquote() << "restore" << title << restored.how << "model" << model / 1e6
        << "ms, publish" << (published - model) / 1e6 << "ms, after" << (clock.nsecsElapsed() - published) / 1e6 << "ms";
    return wellResult(restored);
}

QVariantMap WorkspaceStore::activateWellEntry(const QString &entryId)
{
    const WellEntry *entry = m_model.wellEntry(entryId);
    if (!entry) {
        return failed(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer in the Well.")));
    }
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }
    const Wells::EntryState state = Wells::entryState(m_model, *entry);
    if (state == Wells::EntryState::Minimized) {
        return restoreMinimizedView(entry->minimized.last().viewId);
    }

    const QString title = entry->title.isEmpty() ? QStringLiteral("it") : QStringLiteral("“%1”").arg(entry->title);
    const QString typeId = entry->typeId;
    // Opening it again is a step; bringing it forward is only focus.
    HistoryStep step = historyStep(QStringLiteral("Opened %1").arg(title));
    const QString previous = m_model.focusedAnchorId();
    const Wells::Restored activated = Wells::activate(m_model, entryId);
    if (!activated.result.ok) {
        return failed(activated.result);
    }
    if (activated.how == QStringLiteral("opened")) {
        if (const ViewTypeDescriptor *type = m_registry.find(typeId); type && type->stateVersion > 1) {
            m_model.setViewType(activated.viewId, typeId, type->stateVersion);
        }
        recordStep(std::move(step));
        publish();
        emit viewOpened(activated.viewId);
    } else {
        publish();
    }
    tuckAwayFloatingPanels(previous);
    emit viewFocused(activated.viewId);
    return wellResult(activated);
}

QVariantMap WorkspaceStore::setViewPinned(const QString &viewId, bool pinned)
{
    const ViewRecord *view = m_model.view(viewId);
    if (!view) {
        return failed(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open.")));
    }
    const QString title = view->title.isEmpty() ? QStringLiteral("a tab") : QStringLiteral("“%1”").arg(view->title);
    HistoryStep step = historyStep((pinned ? QStringLiteral("Pinned %1") : QStringLiteral("Unpinned %1")).arg(title));
    const OpResult result = m_model.setViewPinned(viewId, pinned);
    if (!result.ok) {
        return failed(result);
    }
    recordStep(std::move(step));
    publish();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("entryId"), result.createdId}};
}

QVariantMap WorkspaceStore::setWellEntryPinned(const QString &entryId, bool pinned)
{
    const WellEntry *entry = m_model.wellEntry(entryId);
    if (!entry) {
        return failed(OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer in the Well.")));
    }
    const QString title = entry->title.isEmpty() ? QStringLiteral("it") : QStringLiteral("“%1”").arg(entry->title);
    HistoryStep step = historyStep((pinned ? QStringLiteral("Pinned %1") : QStringLiteral("Unpinned %1")).arg(title));
    const OpResult result = m_model.setWellEntryPinned(entryId, pinned);
    if (!result.ok) {
        return failed(result);
    }
    recordStep(std::move(step));
    publish();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("entryId"), entryId}};
}

bool WorkspaceStore::setWellPosition(const QString &wellId, double x, double y)
{
    const int before = m_model.revision();
    if (!m_model.setWellPosition(wellId, QPointF(x, y)).ok) {
        return false;
    }
    if (m_model.revision() != before) {
        publish();
    }
    return true;
}
