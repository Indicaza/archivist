#include "Core/Domains/Workspace/workspace_placement.h"

#include "Core/Domains/Workspace/view_type_registry.h"

#include <algorithm>

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

bool isFileFamily(const QString &typeId)
{
    return isDocumentType(typeId) || typeId == ViewTypes::Welcome;
}

bool anyFileView(const WorkspaceModel &model, const AnchorRecord &anchor)
{
    for (const QString &viewId : anchor.views) {
        const ViewRecord *view = model.view(viewId);
        if (view && isDocumentType(view->typeId)) {
            return true;
        }
    }
    return false;
}

bool holdsType(const WorkspaceModel &model, const AnchorRecord &anchor, const QString &typeId)
{
    for (const QString &viewId : anchor.views) {
        const ViewRecord *view = model.view(viewId);
        if (view && view->typeId == typeId) {
            return true;
        }
    }
    return false;
}

// Would a file look at home here? Empty panels, panels showing files, and
// panels meant for files do; chat and terminal panels don't.
bool holdsFiles(const WorkspaceModel &model, const AnchorRecord &anchor)
{
    for (const QString &role : anchor.roles) {
        if (isDocumentType(role)) {
            return true;
        }
    }
    if (!anchor.roles.isEmpty()) {
        return false;
    }
    for (const QString &viewId : anchor.views) {
        const ViewRecord *view = model.view(viewId);
        if (view && isFileFamily(view->typeId)) {
            return true;
        }
    }
    return anchor.views.isEmpty();
}

// Chats, terminals and other tools: a panel meant for the type, else one
// already showing it (in the same worktree when there's a choice), else
// the usual place.
QString chooseForTool(const WorkspaceModel &model, const QString &typeId, const ResourceRef &resource)
{
    QString holding;
    QString holdingSameWorktree;

    for (const QString &anchorId : model.anchorIds()) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        if (!isOpen(model, anchorId)) {
            continue;
        }
        if (anchor->roles.contains(typeId)) {
            return anchorId;
        }
        for (const QString &viewId : anchor->views) {
            const ViewRecord *view = model.view(viewId);
            if (!view || view->typeId != typeId) {
                continue;
            }
            if (holding.isEmpty()) {
                holding = anchorId;
            }
            if (holdingSameWorktree.isEmpty() && !resource.libraryId.isEmpty() && view->resource.libraryId == resource.libraryId) {
                holdingSameWorktree = anchorId;
            }
        }
    }

    if (!holdingSameWorktree.isEmpty()) {
        return holdingSameWorktree;
    }
    return holding.isEmpty() ? fallback(model) : holding;
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
    if (!isDocumentType(typeId) && !typeId.isEmpty()) {
        return chooseForTool(model, typeId, resource);
    }

    // Files go to the panel the person is working in when it shows files,
    // else to a panel meant for them. Never into a chat or terminal panel,
    // and not into a panel bound to another worktree, while a better one
    // exists.
    QString best;
    int bestScore = 0;
    const QStringList anchors = model.anchorIds();

    for (const QString &anchorId : anchors) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        if (!isOpen(model, anchorId) || !holdsFiles(model, *anchor)) {
            continue;
        }
        const bool otherWorktree = !anchor->libraryId.isEmpty() && !resource.libraryId.isEmpty() && anchor->libraryId != resource.libraryId;
        if (otherWorktree) {
            continue;
        }

        int score = 1;
        if (anchorId == model.explicitTargetAnchorId()) {
            score += 200;
        }
        if (anchorId == model.focusedAnchorId()) {
            score += 80;
        }
        const qsizetype recent = hints.recentAnchors.indexOf(anchorId);
        if (recent >= 0) {
            score += std::max<qsizetype>(10, 60 - recent * 10);
        }
        if (!typeId.isEmpty() && anchor->roles.contains(typeId)) {
            score += 50;
        }
        if (!anchor->libraryId.isEmpty() && anchor->libraryId == resource.libraryId) {
            score += 40;
        }
        if (holdsType(model, *anchor, typeId)) {
            score += 20;
        } else if (anyFileView(model, *anchor)) {
            score += 15;
        }
        if (model.surfaceOfAnchor(anchorId) == model.primarySurfaceId()) {
            score += 3;
        }
        if (score > bestScore) {
            best = anchorId;
            bestScore = score;
        }
    }

    return best.isEmpty() ? fallback(model) : best;
}
} // namespace Workspace::Placement
