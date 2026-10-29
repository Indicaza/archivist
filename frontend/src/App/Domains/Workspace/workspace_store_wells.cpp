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
    const OpResult result = Wells::minimize(m_model, viewId);
    if (!result.ok) {
        return failed(result);
    }
    recordStep(std::move(step));
    publish();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("entryId"), result.createdId}};
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
    const QString title = view->title.isEmpty() ? QStringLiteral("a tab") : QStringLiteral("“%1”").arg(view->title);
    HistoryStep step = historyStep(QStringLiteral("Restored %1").arg(title));
    const Wells::Restored restored = Wells::restore(m_model, viewId);
    if (!restored.result.ok) {
        return failed(restored.result);
    }
    recordStep(std::move(step));
    publish();
    emit viewFocused(viewId);
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
