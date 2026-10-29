#include "Core/Domains/Workspace/workspace_wells.h"

#include "Core/Domains/Workspace/workspace_commands.h"
#include "Core/Domains/Workspace/workspace_magnets.h"

namespace Workspace::Wells
{
QString entryStateName(EntryState state)
{
    switch (state) {
    case EntryState::Minimized: return QStringLiteral("minimized");
    case EntryState::PinnedOpen: return QStringLiteral("pinnedOpen");
    case EntryState::PinnedClosed: return QStringLiteral("pinnedClosed");
    }
    return QStringLiteral("minimized");
}

QStringList openViews(const WorkspaceModel &model, const WellEntry &entry)
{
    if (entry.resource.isEmpty()) {
        return {};
    }
    return model.viewsForResource(entry.resource.kind, entry.resource.id);
}

EntryState entryState(const WorkspaceModel &model, const WellEntry &entry)
{
    if (!entry.minimized.isEmpty()) {
        return EntryState::Minimized;
    }
    return openViews(model, entry).isEmpty() ? EntryState::PinnedClosed : EntryState::PinnedOpen;
}

OpResult minimize(WorkspaceModel &model, const QString &viewId, const QString &wellId, Origin origin)
{
    if (!model.view(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open."));
    }
    const Place place = Magnets::placeOf(model, DragSource::view(viewId));
    return model.minimizeView(viewId, place, wellId, origin);
}

QString fallbackAnchor(const WorkspaceModel &model, const QString &typeId)
{
    for (const QString &anchorId : model.anchorIds(model.primarySurfaceId())) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        const ViewRecord *active = anchor ? model.view(anchor->activeViewId) : nullptr;
        if (active && active->typeId == typeId) {
            return anchorId;
        }
    }
    return model.targetAnchorId();
}

Restored restore(WorkspaceModel &model, const QString &viewId)
{
    Restored out;
    out.viewId = viewId;
    const MinimizedView *minimized = model.minimizedView(viewId);
    if (!minimized) {
        out.result = OpResult::failure(QStringLiteral("NOT_MINIMIZED"), QStringLiteral("It isn't put away."));
        return out;
    }
    const Place place = minimized->place;
    const QString typeId = model.view(viewId)->typeId;

    // Worked out on a copy, so a restore lands whole or not at all.
    WorkspaceModel work = model;

    if (work.anchor(place.anchorId)) {
        // Its panel is still there: back into it, at its old position.
        out.result = work.unminimizeView(viewId, place.anchorId, place.index);
        out.how = QStringLiteral("original");
    } else {
        // Its panel is gone. Join the fallback panel first, then go where the
        // old panel sat, as Return to Previous Position would.
        const QString fallback = fallbackAnchor(work, typeId);
        out.result = work.unminimizeView(viewId, fallback, -1);
        out.how = QStringLiteral("fallback");
        if (out.result.ok && place.isValid()) {
            WorkspaceModel attempt = work;
            const Magnets::ReturnPlan plan = Magnets::planReturn(attempt, place);
            OpResult moved = plan.result;
            if (moved.ok) {
                moved = plan.detach
                    ? attempt.detachView(viewId, plan.geometry, plan.screenName, Origin::User, plan.inMainWindow)
                    : attempt.applyDrop(DragSource::view(viewId), plan.target);
            }
            if (moved.ok && plan.detach && !plan.sidebarSlot.isEmpty()) {
                moved = attempt.setSurfaceSidebarSlot(attempt.surfaceOfAnchor(attempt.anchorOfView(viewId)), plan.sidebarSlot);
            }
            if (moved.ok) {
                if (!plan.detach && plan.target.edge != Edge::Center) {
                    Magnets::restoreShare(attempt, attempt.anchorOfView(viewId), place);
                }
                work = attempt;
                out.how = QStringLiteral("returned");
            }
        }
    }

    if (!out.result.ok) {
        return out;
    }
    out.anchorId = work.anchorOfView(viewId);
    work.activateView(viewId);
    work.focusAnchor(out.anchorId);
    model = work;
    return out;
}

Restored activate(WorkspaceModel &model, const QString &entryId)
{
    Restored out;
    const WellEntry *found = model.wellEntry(entryId);
    if (!found) {
        out.result = OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer in the Well."));
        return out;
    }
    const WellEntry entry = *found;

    switch (entryState(model, entry)) {
    case EntryState::Minimized:
        return restore(model, entry.minimized.last().viewId);
    case EntryState::PinnedOpen: {
        // The view showing it in the focused panel wins; else the first.
        const QStringList views = openViews(model, entry);
        QString viewId = views.first();
        const AnchorRecord *focused = model.anchor(model.focusedAnchorId());
        for (const QString &candidate : views) {
            if (focused && focused->views.contains(candidate)) {
                viewId = candidate;
            }
        }
        out.viewId = viewId;
        out.anchorId = model.anchorOfView(viewId);
        out.how = QStringLiteral("revealed");
        out.result = model.activateView(viewId);
        if (out.result.ok) {
            model.focusAnchor(out.anchorId);
        }
        return out;
    }
    case EntryState::PinnedClosed: {
        ViewRecord view;
        view.typeId = entry.typeId;
        view.resource = entry.resource;
        view.title = entry.title;
        const QString anchorId = fallbackAnchor(model, entry.typeId);
        out.result = model.openView(view, anchorId);
        out.how = QStringLiteral("opened");
        if (out.result.ok) {
            out.viewId = out.result.createdId;
            out.anchorId = model.anchorOfView(out.viewId);
            model.focusAnchor(out.anchorId);
        }
        return out;
    }
    }
    return out;
}

QVariantList wellsList(const WorkspaceModel &model)
{
    QVariantList wells;
    for (const WellRecord &well : model.wells()) {
        QVariantList entries;
        for (const WellEntry &entry : well.entries) {
            QStringList minimized;
            for (const MinimizedView &view : entry.minimized) {
                minimized.append(view.viewId);
            }
            const ViewRecord *latest = entry.minimized.isEmpty() ? nullptr : model.view(entry.minimized.last().viewId);
            entries.append(QVariantMap{
                {QStringLiteral("entryId"), entry.id},
                {QStringLiteral("state"), entryStateName(entryState(model, entry))},
                {QStringLiteral("pinned"), entry.pinned},
                {QStringLiteral("minimized"), !entry.minimized.isEmpty()},
                {QStringLiteral("minimizedViewIds"), minimized},
                {QStringLiteral("openViewIds"), openViews(model, entry)},
                {QStringLiteral("typeId"), latest ? latest->typeId : entry.typeId},
                {QStringLiteral("title"), latest && !latest->title.isEmpty() ? latest->title : entry.title},
                {QStringLiteral("resourceKind"), entry.resource.kind},
                {QStringLiteral("resourceId"), entry.resource.id},
                // For its icon and name (a file's extension, a chat's id).
                {QStringLiteral("resource"), Commands::resourceToMap(latest ? latest->resource : entry.resource)},
            });
        }
        wells.append(QVariantMap{
            {QStringLiteral("wellId"), well.id},
            {QStringLiteral("surfaceId"), well.surfaceId},
            {QStringLiteral("x"), well.position.x()},
            {QStringLiteral("y"), well.position.y()},
            {QStringLiteral("orientation"), orientationName(well.orientation)},
            {QStringLiteral("form"), wellFormName(well.form)},
            {QStringLiteral("entries"), entries},
        });
    }
    return wells;
}
} // namespace Workspace::Wells
