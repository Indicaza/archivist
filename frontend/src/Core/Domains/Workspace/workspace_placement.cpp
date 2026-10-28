#include "Core/Domains/Workspace/workspace_placement.h"

#include "Core/Domains/Workspace/view_type_registry.h"

namespace Workspace::Placement
{
namespace
{
bool isDocumentType(const QString &typeId)
{
    return typeId == ViewTypes::Editor || typeId == ViewTypes::Preview || typeId == ViewTypes::Diff;
}

// Content-locked panels keep what they hold: new content goes elsewhere
// unless the person points it there (or nowhere else exists).
bool isOpen(const WorkspaceModel &model, const QString &anchorId)
{
    const AnchorRecord *anchor = model.anchor(anchorId);
    return anchor && !anchor->contentLocked;
}

QString fallback(const WorkspaceModel &model)
{
    const QString target = model.targetAnchorId();
    if (isOpen(model, target)) {
        return target;
    }
    if (isOpen(model, model.focusedAnchorId())) {
        return model.focusedAnchorId();
    }
    for (const QString &anchorId : model.anchorIds(model.primarySurfaceId())) {
        if (isOpen(model, anchorId)) {
            return anchorId;
        }
    }
    for (const QString &anchorId : model.anchorIds()) {
        if (isOpen(model, anchorId)) {
            return anchorId;
        }
    }
    return target;
}
} // namespace

bool canReceive(const WorkspaceModel &model, const QString &anchorId, Origin origin)
{
    const AnchorRecord *anchor = model.anchor(anchorId);
    return anchor && (origin == Origin::User || !anchor->contentLocked);
}

QString existingView(const WorkspaceModel &model, const QString &typeId, const ResourceRef &resource, const QString &preferredAnchorId)
{
    if (resource.isEmpty()) {
        return {};
    }

    const QString preferred = preferredAnchorId.isEmpty() ? model.targetAnchorId() : preferredAnchorId;
    QString existing;

    for (const QString &viewId : model.viewsForResource(resource.kind, resource.id)) {
        const ViewRecord *view = model.view(viewId);
        if (!view || view->typeId != typeId) {
            continue;
        }
        if (existing.isEmpty() || model.anchorOfView(viewId) == preferred) {
            existing = viewId;
        }
    }

    return existing;
}

QString chooseAnchor(const WorkspaceModel &model, const QString &typeId, const ResourceRef &resource, const Hints &hints)
{
    Q_UNUSED(resource)
    Q_UNUSED(hints)

    if (isDocumentType(typeId) || typeId.isEmpty()) {
        return fallback(model);
    }

    QString holding;

    for (const QString &anchorId : model.anchorIds()) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        if (!isOpen(model, anchorId)) {
            continue;
        }
        if (anchor->roles.contains(typeId)) {
            return anchorId;
        }
        if (holding.isEmpty()) {
            for (const QString &viewId : anchor->views) {
                const ViewRecord *view = model.view(viewId);
                if (view && view->typeId == typeId) {
                    holding = anchorId;
                    break;
                }
            }
        }
    }

    return holding.isEmpty() ? fallback(model) : holding;
}
} // namespace Workspace::Placement
