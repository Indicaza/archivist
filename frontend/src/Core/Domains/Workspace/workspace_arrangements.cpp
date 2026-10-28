#include "Core/Domains/Workspace/workspace_arrangements.h"

#include <cmath>

namespace Workspace::Arrangements
{
namespace
{
const QSizeF kTypicalSurface(1440, 900);

QSizeF sizeOf(const QHash<QString, QSizeF> &sizes, const QString &surfaceId)
{
    const QSizeF size = sizes.value(surfaceId);
    return size.width() > 1 && size.height() > 1 ? size : kTypicalSurface;
}

bool sameRect(const QRectF &a, const QRectF &b)
{
    constexpr double tolerance = 2.0;
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance
        && std::abs(a.width() - b.width()) <= tolerance && std::abs(a.height() - b.height()) <= tolerance;
}

QString firstAnchorOfPrimary(const WorkspaceModel::Data &data)
{
    for (const QString &surfaceId : data.surfaceOrder) {
        const SurfaceRecord surface = data.surfaces.value(surfaceId);
        if (!surface.primary) {
            continue;
        }
        // Walk the tree to its first leaf.
        QString nodeId = surface.rootNodeId;
        while (data.nodes.contains(nodeId)) {
            const SplitNode &node = data.nodes[nodeId];
            if (node.leaf) {
                return node.anchorId;
            }
            if (node.children.isEmpty()) {
                break;
            }
            nodeId = node.children.first();
        }
    }
    return {};
}
} // namespace

Result restore(
    WorkspaceModel &model,
    const WorkspaceModel::Data &arrangement,
    const QSet<QString> &closable,
    const QHash<QString, QSizeF> &surfaceSizes,
    const LayoutMetrics &metrics,
    const QSet<QString> &reopen
)
{
    Result result;
    const WorkspaceModel::Data current = model.data();
    WorkspaceModel::Data out = arrangement;

    // Views: keep what still exists, drop what was closed since.
    QHash<QString, QString> placed; // view -> anchor in `out`
    for (auto it = out.anchors.begin(); it != out.anchors.end(); ++it) {
        QStringList kept;
        for (const QString &viewId : std::as_const(it->views)) {
            const bool reopened = !current.views.contains(viewId) && reopen.contains(viewId) && arrangement.views.contains(viewId);
            if ((current.views.contains(viewId) && !closable.contains(viewId)) || reopened) {
                kept.append(viewId);
                placed.insert(viewId, it.key());
            }
            if (reopened) {
                result.reopenedViews.append(viewId);
            }
        }
        it->views = kept;
    }

    out.views.clear();
    for (const QString &viewId : std::as_const(result.reopenedViews)) {
        out.views.insert(viewId, arrangement.views.value(viewId));
    }
    QString fallback = out.anchors.contains(out.targetAnchorId) ? out.targetAnchorId : firstAnchorOfPrimary(out);

    for (auto it = current.views.cbegin(); it != current.views.cend(); ++it) {
        if (closable.contains(it.key())) {
            result.closedViews.append(it.key());
            continue;
        }
        out.views.insert(it.key(), it.value());
        if (placed.contains(it.key())) {
            continue;
        }
        // Opened since: stay in its panel when that panel comes back.
        QString home;
        for (auto anchor = current.anchors.cbegin(); anchor != current.anchors.cend(); ++anchor) {
            if (anchor->views.contains(it.key())) {
                home = anchor.key();
                break;
            }
        }
        if (!out.anchors.contains(home)) {
            home = fallback;
        }
        if (!out.anchors.contains(home)) {
            result.code = QStringLiteral("INVALID_ARRANGEMENT");
            result.message = QStringLiteral("That arrangement has nowhere to put open tabs.");
            return result;
        }
        out.anchors[home].views.append(it.key());
        placed.insert(it.key(), home);
    }

    // Panel configuration follows the person's latest choices (names come
    // back with the arrangement: agents name panels too).
    for (auto it = out.anchors.begin(); it != out.anchors.end(); ++it) {
        const auto now = current.anchors.constFind(it.key());
        if (now != current.anchors.cend()) {
            it->layoutLocked = now->layoutLocked;
            it->contentLocked = now->contentLocked;
            it->switchable = now->switchable;
            it->libraryId = now->libraryId;
            it->roles = now->roles;
        }
        if (!it->views.contains(it->activeViewId)) {
            const QString currentActive = now != current.anchors.cend() ? now->activeViewId : QString();
            it->activeViewId = it->views.contains(currentActive) ? currentActive : it->views.value(0);
        }
    }

    // Windows stay where the person has them on screen.
    for (auto it = out.surfaces.begin(); it != out.surfaces.end(); ++it) {
        const auto now = current.surfaces.constFind(it.key());
        if (now != current.surfaces.cend()) {
            it->geometry = now->geometry;
            it->screenName = now->screenName;
            it->preferredGeometry = now->preferredGeometry;
            it->preferredScreenName = now->preferredScreenName;
            it->windowMaximized = now->windowMaximized;
        }
        if (!it->maximizedAnchorId.isEmpty() && !out.anchors.contains(it->maximizedAnchorId)) {
            it->maximizedAnchorId.clear();
        }
    }

    out.focusedAnchorId = out.anchors.contains(current.focusedAnchorId) ? current.focusedAnchorId
        : out.anchors.contains(arrangement.focusedAnchorId)              ? arrangement.focusedAnchorId
                                                                          : fallback;
    out.targetAnchorId = out.anchors.contains(current.targetAnchorId) ? current.targetAnchorId
        : out.anchors.contains(arrangement.targetAnchorId)            ? arrangement.targetAnchorId
                                                                       : QString();

    WorkspaceModel candidate = model;
    const QStringList problems = candidate.setData(out);
    if (!problems.isEmpty()) {
        result.code = QStringLiteral("INVALID_ARRANGEMENT");
        result.message = QStringLiteral("That arrangement can't be restored: ") + problems.join(QStringLiteral("; "));
        return result;
    }

    // Panels that only existed for what was just closed fold away again.
    for (const QString &anchorId : candidate.anchorIds()) {
        const AnchorRecord *anchor = candidate.anchor(anchorId);
        const auto before = arrangement.anchors.constFind(anchorId);
        const bool wasFilled = before != arrangement.anchors.cend() && !before->views.isEmpty();
        if (anchor && anchor->views.isEmpty() && !anchor->pinned && wasFilled && !anchor->layoutLocked && anchor->name.isEmpty()
            && candidate.anchorIds().size() > 1) {
            candidate.closeAnchor(anchorId, Origin::User);
        }
    }

    // A panel locked in place stays exactly where it is.
    for (const QString &anchorId : model.layoutLockedAnchorIds()) {
        const QString surfaceNow = model.surfaceOfAnchor(anchorId);
        const QString surfaceThen = candidate.surfaceOfAnchor(anchorId);
        const QRectF now = model.computeGeometry(surfaceNow, sizeOf(surfaceSizes, surfaceNow), metrics).anchors.value(anchorId);
        const QRectF then = surfaceThen.isEmpty()
            ? QRectF()
            : candidate.computeGeometry(surfaceThen, sizeOf(surfaceSizes, surfaceThen), metrics).anchors.value(anchorId);
        if (surfaceNow != surfaceThen || !sameRect(now, then)) {
            result.code = QStringLiteral("LAYOUT_LOCKED");
            result.message = QStringLiteral("“%1” is locked in place, and putting things back would move it. Unlock it first.")
                                 .arg(model.anchorDisplayName(anchorId));
            result.closedViews.clear();
            result.reopenedViews.clear();
            return result;
        }
    }

    model = candidate;
    result.ok = true;
    return result;
}
} // namespace Workspace::Arrangements
