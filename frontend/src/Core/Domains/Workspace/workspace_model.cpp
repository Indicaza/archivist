#include "Core/Domains/Workspace/workspace_model.h"

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace Workspace
{
QString edgeName(Edge edge)
{
    switch (edge) {
    case Edge::Center: return QStringLiteral("center");
    case Edge::Left: return QStringLiteral("left");
    case Edge::Right: return QStringLiteral("right");
    case Edge::Top: return QStringLiteral("top");
    case Edge::Bottom: return QStringLiteral("bottom");
    }
    return QStringLiteral("center");
}

bool parseEdge(const QString &name, Edge *edge)
{
    static const QHash<QString, Edge> edges{
        {QStringLiteral("center"), Edge::Center},
        {QStringLiteral("left"), Edge::Left},
        {QStringLiteral("right"), Edge::Right},
        {QStringLiteral("top"), Edge::Top},
        {QStringLiteral("bottom"), Edge::Bottom},
    };
    const auto it = edges.constFind(name.toLower());

    if (it == edges.constEnd()) {
        return false;
    }

    *edge = it.value();
    return true;
}

QString orientationName(Orientation orientation)
{
    return orientation == Orientation::Horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical");
}

bool parseOrientation(const QString &name, Orientation *orientation)
{
    if (name == QStringLiteral("horizontal")) {
        *orientation = Orientation::Horizontal;
        return true;
    }

    if (name == QStringLiteral("vertical")) {
        *orientation = Orientation::Vertical;
        return true;
    }

    return false;
}

QString wellFormName(WellForm form)
{
    switch (form) {
    case WellForm::Linear: return QStringLiteral("linear");
    }
    return QStringLiteral("linear");
}

bool parseWellForm(const QString &name, WellForm *form)
{
    if (name == QStringLiteral("linear")) {
        *form = WellForm::Linear;
        return true;
    }
    return false;
}

Orientation orientationForEdge(Edge edge)
{
    return edge == Edge::Top || edge == Edge::Bottom ? Orientation::Vertical : Orientation::Horizontal;
}

bool edgeLeads(Edge edge)
{
    return edge == Edge::Left || edge == Edge::Top;
}

QString originName(Origin origin)
{
    switch (origin) {
    case Origin::User: return QStringLiteral("user");
    case Origin::Automatic: return QStringLiteral("automatic");
    case Origin::Agent: return QStringLiteral("agent");
    }
    return QStringLiteral("user");
}

bool parseOrigin(const QString &name, Origin *origin)
{
    const QString lower = name.toLower();
    if (lower == QStringLiteral("user")) {
        *origin = Origin::User;
    } else if (lower == QStringLiteral("automatic")) {
        *origin = Origin::Automatic;
    } else if (lower == QStringLiteral("agent")) {
        *origin = Origin::Agent;
    } else {
        return false;
    }
    return true;
}

QString librarySourceName(LibraryResolution::Source source)
{
    switch (source) {
    case LibraryResolution::Source::None: return QStringLiteral("none");
    case LibraryResolution::Source::AnchorBinding: return QStringLiteral("anchor");
    case LibraryResolution::Source::ActiveView: return QStringLiteral("activeView");
    case LibraryResolution::Source::Explorer: return QStringLiteral("explorer");
    case LibraryResolution::Source::Default: return QStringLiteral("default");
    }
    return QStringLiteral("none");
}

namespace
{
constexpr double kMinimumFraction = 0.02;
constexpr double kSurfaceEdgeFraction = 0.3;

QList<double> normalized(QList<double> sizes)
{
    double total = 0;

    for (double &size : sizes) {
        if (!std::isfinite(size) || size < kMinimumFraction) {
            size = kMinimumFraction;
        }
        total += size;
    }

    if (total <= 0) {
        return sizes;
    }

    for (double &size : sizes) {
        size /= total;
    }

    return sizes;
}

double area(const QRectF &rect)
{
    return rect.isValid() ? rect.width() * rect.height() : 0.0;
}

QString defaultId(const QString &prefix)
{
    return prefix + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::Id128).left(12);
}
} // namespace

WorkspaceModel::WorkspaceModel()
    : WorkspaceModel(IdGenerator(defaultId))
{
}

WorkspaceModel::WorkspaceModel(IdGenerator generator)
    : m_generateId(std::move(generator))
{
    const QString surfaceId = createSurface({}, {});
    m_surfaces[surfaceId].primary = true;
    ensurePrimaryHasAnchor();
    ensureMainWell();
    m_focusedAnchorId = anchorIds(surfaceId).value(0);
}

// ---- transactions --------------------------------------------------------

// Runs `work` against the model; on failure, or when the result would break
// an invariant, the model is put back exactly as it was.
template<typename Work>
OpResult WorkspaceModel::transact(Work work)
{
    const Data before = data();
    const int revision = m_revision;
    OpResult result = work();

    if (!result.ok) {
        setData(before);
        m_revision = revision;
        return result;
    }

    const QStringList problems = validate();

    if (!problems.isEmpty()) {
        setData(before);
        m_revision = revision;
        return OpResult::failure(QStringLiteral("INVARIANT"), problems.join(QStringLiteral("; ")));
    }

    return result;
}

// ---- reading ---------------------------------------------------------------

QString WorkspaceModel::primarySurfaceId() const
{
    for (const QString &surfaceId : m_surfaceOrder) {
        if (m_surfaces.value(surfaceId).primary) {
            return surfaceId;
        }
    }
    return {};
}

const SurfaceRecord *WorkspaceModel::surface(const QString &surfaceId) const
{
    const auto it = m_surfaces.constFind(surfaceId);
    return it == m_surfaces.constEnd() ? nullptr : &it.value();
}

const AnchorRecord *WorkspaceModel::anchor(const QString &anchorId) const
{
    const auto it = m_anchors.constFind(anchorId);
    return it == m_anchors.constEnd() ? nullptr : &it.value();
}

const ViewRecord *WorkspaceModel::view(const QString &viewId) const
{
    const auto it = m_views.constFind(viewId);
    return it == m_views.constEnd() ? nullptr : &it.value();
}

const SplitNode *WorkspaceModel::node(const QString &nodeId) const
{
    const auto it = m_nodes.constFind(nodeId);
    return it == m_nodes.constEnd() ? nullptr : &it.value();
}

void WorkspaceModel::collectAnchors(const QString &nodeId, QStringList &out) const
{
    const auto it = m_nodes.constFind(nodeId);

    if (it == m_nodes.constEnd()) {
        return;
    }

    if (it->leaf) {
        out.append(it->anchorId);
        return;
    }

    for (const QString &child : it->children) {
        collectAnchors(child, out);
    }
}

QStringList WorkspaceModel::anchorIds() const
{
    QStringList result;

    for (const QString &surfaceId : m_surfaceOrder) {
        collectAnchors(m_surfaces.value(surfaceId).rootNodeId, result);
    }

    return result;
}

QStringList WorkspaceModel::anchorIds(const QString &surfaceId) const
{
    QStringList result;
    collectAnchors(m_surfaces.value(surfaceId).rootNodeId, result);
    return result;
}

QStringList WorkspaceModel::viewIds() const
{
    QStringList result;

    for (const QString &anchorId : anchorIds()) {
        result.append(m_anchors.value(anchorId).views);
    }

    return result;
}

QString WorkspaceModel::anchorOfView(const QString &viewId) const
{
    return m_anchorOfView.value(viewId);
}

QString WorkspaceModel::leafOfAnchor(const QString &anchorId) const
{
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (it->leaf && it->anchorId == anchorId) {
            return it.key();
        }
    }
    return {};
}

QString WorkspaceModel::rootOfNode(const QString &nodeId) const
{
    QString current = nodeId;
    QSet<QString> seen;

    while (m_nodes.contains(current) && !m_nodes.value(current).parentId.isEmpty()) {
        if (seen.contains(current)) {
            return {};
        }
        seen.insert(current);
        current = m_nodes.value(current).parentId;
    }

    return m_nodes.contains(current) ? current : QString();
}

QString WorkspaceModel::surfaceOfNode(const QString &nodeId) const
{
    const QString root = rootOfNode(nodeId);

    for (auto it = m_surfaces.constBegin(); it != m_surfaces.constEnd(); ++it) {
        if (!root.isEmpty() && it->rootNodeId == root) {
            return it.key();
        }
    }
    return {};
}

QString WorkspaceModel::surfaceOfAnchor(const QString &anchorId) const
{
    return surfaceOfNode(leafOfAnchor(anchorId));
}

QStringList WorkspaceModel::viewsForResource(const QString &kind, const QString &id) const
{
    QStringList result;

    for (const QString &viewId : viewIds()) {
        const ViewRecord &record = m_views[viewId];
        if (record.resource.kind == kind && record.resource.id == id) {
            result.append(viewId);
        }
    }

    return result;
}

QString WorkspaceModel::targetAnchorId() const
{
    if (m_anchors.contains(m_targetAnchorId)) {
        return m_targetAnchorId;
    }

    if (m_anchors.contains(m_focusedAnchorId)) {
        return m_focusedAnchorId;
    }

    return anchorIds(primarySurfaceId()).value(0);
}

// ---- tree plumbing -------------------------------------------------------

QString WorkspaceModel::newId(const QString &prefix)
{
    for (;;) {
        const QString id = m_generateId ? m_generateId(prefix) : defaultId(prefix);

        if (!m_nodes.contains(id) && !m_anchors.contains(id) && !m_views.contains(id) && !m_surfaces.contains(id)) {
            return id;
        }
    }
}

QString WorkspaceModel::createLeaf(const QString &anchorId)
{
    SplitNode leaf;
    leaf.id = newId(QStringLiteral("n"));
    leaf.leaf = true;
    leaf.anchorId = anchorId;
    m_nodes.insert(leaf.id, leaf);
    return leaf.id;
}

QString WorkspaceModel::createSurface(const QRectF &geometry, const QString &screenName, bool inMainWindow)
{
    SurfaceRecord record;
    record.id = newId(QStringLiteral("w"));
    m_surfaces.insert(record.id, record);
    m_surfaceOrder.append(record.id);
    placeSurface(record.id, geometry, screenName, inMainWindow);
    return record.id;
}

void WorkspaceModel::placeSurface(const QString &surfaceId, const QRectF &geometry, const QString &screenName, bool inMainWindow)
{
    SurfaceRecord &record = m_surfaces[surfaceId];
    const QString screen = inMainWindow ? QString() : screenName;
    record.inMainWindow = inMainWindow;
    record.sidebarSlot.clear();
    record.geometry = geometry;
    record.screenName = screen;
    record.preferredGeometry = geometry;
    record.preferredScreenName = screen;
    if (inMainWindow) {
        record.windowMaximized = false;
    }
}

void WorkspaceModel::replaceChild(const QString &parentId, const QString &oldId, const QString &newId)
{
    if (parentId.isEmpty()) {
        for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
            if (it->rootNodeId == oldId) {
                it->rootNodeId = newId;
            }
        }
    } else {
        SplitNode &parent = m_nodes[parentId];
        const qsizetype index = parent.children.indexOf(oldId);
        if (index >= 0) {
            parent.children[index] = newId;
        }
    }

    if (m_nodes.contains(newId)) {
        m_nodes[newId].parentId = parentId;
    }
}

void WorkspaceModel::flattenInto(const QString &parentId)
{
    if (!m_nodes.contains(parentId) || m_nodes.value(parentId).leaf) {
        return;
    }

    bool changed = true;

    while (changed) {
        changed = false;
        SplitNode &parent = m_nodes[parentId];

        for (qsizetype i = 0; i < parent.children.size(); ++i) {
            const QString childId = parent.children.at(i);
            const SplitNode child = m_nodes.value(childId);

            if (child.leaf || child.orientation != parent.orientation) {
                continue;
            }

            const double share = parent.sizes.value(i, 1.0 / parent.children.size());
            parent.children.removeAt(i);
            parent.sizes.removeAt(i);

            for (qsizetype j = 0; j < child.children.size(); ++j) {
                parent.children.insert(i + j, child.children.at(j));
                parent.sizes.insert(i + j, share * child.sizes.value(j, 1.0 / child.children.size()));
                m_nodes[child.children.at(j)].parentId = parentId;
            }

            m_nodes.remove(childId);
            parent.sizes = normalized(parent.sizes);
            changed = true;
            break;
        }
    }
}

void WorkspaceModel::detachNode(const QString &nodeId)
{
    if (!m_nodes.contains(nodeId)) {
        return;
    }

    const QString parentId = m_nodes.value(nodeId).parentId;

    if (parentId.isEmpty()) {
        for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
            if (it->rootNodeId == nodeId) {
                it->rootNodeId.clear();
            }
        }
        return;
    }

    SplitNode &parent = m_nodes[parentId];
    const qsizetype index = parent.children.indexOf(nodeId);

    double freed = 0;

    if (index >= 0) {
        parent.children.removeAt(index);
        if (index < parent.sizes.size()) {
            freed = parent.sizes.takeAt(index);
        }
    }

    m_nodes[nodeId].parentId.clear();

    if (parent.children.size() == 1) {
        // A split with one child is just that child.
        const QString onlyChild = parent.children.first();
        const QString grandParent = parent.parentId;
        replaceChild(grandParent, parentId, onlyChild);
        m_nodes.remove(parentId);

        if (!grandParent.isEmpty()) {
            flattenInto(grandParent);
        }
        return;
    }

    // The freed space goes to unlocked siblings, so a locked anchor keeps
    // its size when a neighbour leaves.
    rebalance(parent, freed);
}

void WorkspaceModel::rebalance(SplitNode &split, double freed, qsizetype skipIndex)
{
    double unlocked = 0;
    int unlockedCount = 0;

    for (qsizetype i = 0; i < split.children.size(); ++i) {
        if (i != skipIndex && !containsLayoutLocked(split.children.at(i))) {
            unlocked += split.sizes.value(i);
            ++unlockedCount;
        }
    }

    if (unlockedCount > 0 && unlocked > 0 && unlocked + freed > kMinimumFraction * unlockedCount) {
        const double factor = (unlocked + freed) / unlocked;
        for (qsizetype i = 0; i < split.children.size(); ++i) {
            if (i != skipIndex && !containsLayoutLocked(split.children.at(i))) {
                split.sizes[i] *= factor;
            }
        }
    }

    split.sizes = normalized(split.sizes);
}

bool WorkspaceModel::isLayoutLocked(const QString &anchorId) const
{
    const auto it = m_anchors.constFind(anchorId);
    return it != m_anchors.constEnd() && it->layoutLocked;
}

bool WorkspaceModel::containsLayoutLocked(const QString &nodeId) const
{
    QStringList anchors;
    collectAnchors(nodeId, anchors);
    for (const QString &anchorId : anchors) {
        if (isLayoutLocked(anchorId)) {
            return true;
        }
    }
    return false;
}

bool WorkspaceModel::isSplitBoundaryLocked(const QString &nodeId, int index) const
{
    const auto it = m_nodes.constFind(nodeId);
    if (it == m_nodes.constEnd() || it->leaf || index < 0 || index + 1 >= it->children.size()) {
        return false;
    }
    return containsLayoutLocked(it->children.at(index)) || containsLayoutLocked(it->children.at(index + 1));
}

QStringList WorkspaceModel::layoutLockedAnchorIds() const
{
    QStringList result;
    for (const QString &anchorId : anchorIds()) {
        if (isLayoutLocked(anchorId)) {
            result.append(anchorId);
        }
    }
    return result;
}

OpResult WorkspaceModel::layoutLockFailure(const QString &anchorId, const QString &consequence) const
{
    if (!isLayoutLocked(anchorId)) {
        return OpResult::success();
    }

    return OpResult::failure(
        QStringLiteral("LAYOUT_LOCKED"),
        QStringLiteral("“%1” has a locked layout, so %2. Unlock it first.").arg(anchorDisplayName(anchorId), consequence)
    );
}

OpResult WorkspaceModel::surfaceEdgeLockFailure(QString surfaceId, Edge edge) const
{
    const QString rootId = m_surfaces.value(surfaceId).rootNodeId;
    const auto root = m_nodes.constFind(rootId);

    if (root == m_nodes.constEnd() || !containsLayoutLocked(rootId)) {
        return OpResult::success();
    }

    // Along the root split, the room comes out of unlocked siblings.
    if (!root->leaf && root->orientation == orientationForEdge(edge)) {
        for (const QString &child : root->children) {
            if (!containsLayoutLocked(child)) {
                return OpResult::success();
            }
        }
    }

    QStringList anchors;
    collectAnchors(rootId, anchors);
    for (const QString &anchorId : anchors) {
        if (isLayoutLocked(anchorId)) {
            return layoutLockFailure(anchorId, QStringLiteral("nothing can be docked along that window edge"));
        }
    }
    return OpResult::success();
}

OpResult WorkspaceModel::contentLockFailure(QString anchorId, Origin origin) const
{
    const auto it = m_anchors.constFind(anchorId);

    if (origin == Origin::User || it == m_anchors.constEnd() || !it->contentLocked) {
        return OpResult::success();
    }

    return OpResult::failure(
        QStringLiteral("CONTENT_LOCKED"),
        QStringLiteral("“%1” has locked content, so its views can't be changed %2.")
            .arg(anchorDisplayName(anchorId), origin == Origin::Agent ? QStringLiteral("by an agent") : QStringLiteral("automatically"))
    );
}

void WorkspaceModel::insertBeside(const QString &targetNodeId, const QString &newNodeId, Edge edge)
{
    const Orientation orientation = orientationForEdge(edge);
    const bool lead = edgeLeads(edge);
    const QString parentId = m_nodes.value(targetNodeId).parentId;

    if (!parentId.isEmpty() && m_nodes.value(parentId).orientation == orientation) {
        SplitNode &parent = m_nodes[parentId];
        const qsizetype index = parent.children.indexOf(targetNodeId);
        const double half = parent.sizes.value(index, 1.0 / parent.children.size()) / 2.0;
        parent.sizes[index] = half;
        const qsizetype insertAt = lead ? index : index + 1;
        parent.children.insert(insertAt, newNodeId);
        parent.sizes.insert(insertAt, half);
        parent.sizes = normalized(parent.sizes);
        m_nodes[newNodeId].parentId = parentId;
        return;
    }

    SplitNode split;
    split.id = newId(QStringLiteral("n"));
    split.leaf = false;
    split.orientation = orientation;
    split.children = lead ? QStringList{newNodeId, targetNodeId} : QStringList{targetNodeId, newNodeId};
    split.sizes = {0.5, 0.5};
    m_nodes.insert(split.id, split);
    replaceChild(parentId, targetNodeId, split.id);
    m_nodes[targetNodeId].parentId = split.id;
    m_nodes[newNodeId].parentId = split.id;
}

void WorkspaceModel::insertAtSurfaceEdge(const QString &surfaceId, const QString &newNodeId, Edge edge)
{
    SurfaceRecord &record = m_surfaces[surfaceId];
    const QString rootId = record.rootNodeId;

    if (rootId.isEmpty()) {
        record.rootNodeId = newNodeId;
        m_nodes[newNodeId].parentId.clear();
        return;
    }

    const Orientation orientation = orientationForEdge(edge);
    const bool lead = edgeLeads(edge);
    SplitNode &root = m_nodes[rootId];

    if (!root.leaf && root.orientation == orientation) {
        const double share = 1.0 / (root.children.size() + 1);
        const qsizetype insertAt = lead ? 0 : root.children.size();
        root.children.insert(insertAt, newNodeId);
        root.sizes.insert(insertAt, share);
        m_nodes[newNodeId].parentId = rootId;
        // The new panel's room comes out of unlocked panels.
        rebalance(root, -share, insertAt);
        return;
    }

    SplitNode split;
    split.id = newId(QStringLiteral("n"));
    split.leaf = false;
    split.orientation = orientation;
    split.children = lead ? QStringList{newNodeId, rootId} : QStringList{rootId, newNodeId};
    split.sizes = lead ? QList<double>{kSurfaceEdgeFraction, 1 - kSurfaceEdgeFraction}
                       : QList<double>{1 - kSurfaceEdgeFraction, kSurfaceEdgeFraction};
    m_nodes.insert(split.id, split);
    record.rootNodeId = split.id;
    m_nodes[rootId].parentId = split.id;
    m_nodes[newNodeId].parentId = split.id;
}

void WorkspaceModel::dropSurfaceIfEmpty(const QString &surfaceId)
{
    if (!m_surfaces.contains(surfaceId)) {
        return;
    }

    const SurfaceRecord record = m_surfaces.value(surfaceId);

    if (!record.rootNodeId.isEmpty() || record.primary) {
        return;
    }

    m_surfaces.remove(surfaceId);
    m_surfaceOrder.removeAll(surfaceId);
}

void WorkspaceModel::ensurePrimaryHasAnchor()
{
    QString primaryId = primarySurfaceId();

    if (primaryId.isEmpty()) {
        primaryId = m_surfaceOrder.value(0);
        if (primaryId.isEmpty()) {
            primaryId = createSurface({}, {});
        }
        m_surfaces[primaryId].primary = true;
    }

    if (!m_surfaces.value(primaryId).rootNodeId.isEmpty()) {
        return;
    }

    AnchorRecord record;
    record.id = newId(QStringLiteral("a"));
    record.pinned = true;
    m_anchors.insert(record.id, record);
    m_surfaces[primaryId].rootNodeId = createLeaf(record.id);
}

void WorkspaceModel::removeAnchorRecord(const QString &anchorId)
{
    const QString surfaceId = surfaceOfAnchor(anchorId);
    const QString leafId = leafOfAnchor(anchorId);

    for (const QString &viewId : m_anchors.value(anchorId).views) {
        m_anchorOfView.remove(viewId);
    }

    detachNode(leafId);
    m_nodes.remove(leafId);
    m_anchors.remove(anchorId);

    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        if (it->maximizedAnchorId == anchorId) {
            it->maximizedAnchorId.clear();
        }
    }

    if (m_targetAnchorId == anchorId) {
        m_targetAnchorId.clear();
    }

    dropSurfaceIfEmpty(surfaceId);
    ensurePrimaryHasAnchor();

    if (m_focusedAnchorId == anchorId) {
        const QStringList sameSurface = anchorIds(surfaceId);
        m_focusedAnchorId = sameSurface.isEmpty() ? anchorIds(primarySurfaceId()).value(0) : sameSurface.first();
    }
}

void WorkspaceModel::collapseIfIncidental(const QString &anchorId)
{
    const AnchorRecord record = m_anchors.value(anchorId);

    // Anchors the user configured (named, locked, switchable) are kept.
    const bool intentional = record.pinned || record.layoutLocked || record.contentLocked || record.switchable
        || !record.name.isEmpty() || !record.libraryId.isEmpty();

    if (!m_anchors.contains(anchorId) || !record.views.isEmpty() || intentional) {
        return;
    }

    const QString surfaceId = surfaceOfAnchor(anchorId);

    // The main window always keeps somewhere to put things.
    if (m_surfaces.value(surfaceId).primary && anchorIds(surfaceId).size() == 1) {
        return;
    }

    removeAnchorRecord(anchorId);
}

void WorkspaceModel::removeViewFromAnchor(const QString &viewId)
{
    const QString anchorId = m_anchorOfView.value(viewId);

    if (anchorId.isEmpty()) {
        return;
    }

    AnchorRecord &record = m_anchors[anchorId];
    const qsizetype index = record.views.indexOf(viewId);
    record.views.removeAll(viewId);
    m_anchorOfView.remove(viewId);

    if (record.activeViewId == viewId) {
        record.activeViewId = record.views.isEmpty()
            ? QString()
            : record.views.at(std::clamp<qsizetype>(index, 0, record.views.size() - 1));
    }
}

void WorkspaceModel::insertViewIntoAnchor(const QString &viewId, const QString &anchorId, int index, bool activate)
{
    AnchorRecord &record = m_anchors[anchorId];
    qsizetype position = index;

    if (position < 0) {
        const qsizetype active = record.views.indexOf(record.activeViewId);
        position = active < 0 ? record.views.size() : active + 1;
    }

    position = std::clamp<qsizetype>(position, 0, record.views.size());
    record.views.insert(position, viewId);
    m_anchorOfView.insert(viewId, anchorId);

    if (activate || record.activeViewId.isEmpty()) {
        record.activeViewId = viewId;
    }
}

// ---- views -------------------------------------------------------------------

OpResult WorkspaceModel::openView(ViewRecord record, QString anchorId, int index, bool activate, Origin origin)
{
    return transact([&]() {
        const QString destination = anchorId.isEmpty() ? targetAnchorId() : anchorId;

        if (!m_anchors.contains(destination)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + destination);
        }

        if (const OpResult locked = contentLockFailure(destination, origin); !locked.ok) {
            return locked;
        }

        if (record.typeId.isEmpty()) {
            return OpResult::failure(QStringLiteral("INVALID_VIEW"), QStringLiteral("A view needs a type."));
        }

        if (record.id.isEmpty()) {
            record.id = newId(QStringLiteral("v"));
        } else if (m_views.contains(record.id)) {
            return OpResult::failure(QStringLiteral("DUPLICATE_ID"), QStringLiteral("View id already in use: ") + record.id);
        }

        m_views.insert(record.id, record);
        insertViewIntoAnchor(record.id, destination, index, activate);
        bump();
        return OpResult::success(record.id);
    });
}

OpResult WorkspaceModel::closeView(QString viewId, Origin origin)
{
    return transact([&]() {
        if (!m_views.contains(viewId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }

        // A minimized view closes from its Well (pinned, its entry stays).
        if (const QString entryId = entryOfMinimizedView(viewId); !entryId.isEmpty()) {
            WellEntry *entry = mutableEntry(entryId);
            entry->minimized.removeIf([&](const MinimizedView &minimized) { return minimized.viewId == viewId; });
            dropEntryIfUnused(entryId);
            m_views.remove(viewId);
            bump();
            return OpResult::success();
        }

        const QString anchorId = m_anchorOfView.value(viewId);

        if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
            return locked;
        }

        removeViewFromAnchor(viewId);
        m_views.remove(viewId);
        collapseIfIncidental(anchorId);
        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::activateView(QString viewId)
{
    if (!m_views.contains(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
    }

    if (isMinimized(viewId)) {
        return OpResult::failure(QStringLiteral("MINIMIZED"), QStringLiteral("It's put away in a Gravity Well: restore it first."));
    }

    const QString anchorId = m_anchorOfView.value(viewId);
    m_anchors[anchorId].activeViewId = viewId;
    m_focusedAnchorId = anchorId;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::updateViewState(QString viewId, const QVariantMap &state)
{
    if (!m_views.contains(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
    }

    m_views[viewId].state = state;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::setViewTitle(QString viewId, QString title)
{
    if (!m_views.contains(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
    }

    m_views[viewId].title = title;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::setViewResource(QString viewId, const ResourceRef &resource, Origin origin)
{
    if (!m_views.contains(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
    }

    if (const OpResult locked = contentLockFailure(m_anchorOfView.value(viewId), origin); !locked.ok) {
        return locked;
    }

    // Pins and minimized entries follow the resource (a rename keeps them).
    const ResourceRef before = m_views[viewId].resource;
    if (!before.isEmpty()) {
        for (WellRecord &well : m_wells) {
            for (WellEntry &entry : well.entries) {
                if (entry.resource.key() == before.key()) {
                    entry.resource = resource;
                }
            }
        }
    }
    m_views[viewId].resource = resource;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::setViewType(QString viewId, QString typeId, int stateVersion)
{
    if (!m_views.contains(viewId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
    }

    if (typeId.isEmpty()) {
        return OpResult::failure(QStringLiteral("INVALID_VIEW"), QStringLiteral("A view needs a type."));
    }

    ViewRecord &record = m_views[viewId];
    record.typeId = typeId;
    record.stateVersion = std::max(1, stateVersion);
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::duplicateView(QString viewId, QString anchorId, Edge edge, Origin origin)
{
    return transact([&]() {
        if (!m_views.contains(viewId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }

        const QString sourceAnchor = m_anchorOfView.value(viewId);
        const QString destination = anchorId.isEmpty() ? sourceAnchor : anchorId;

        if (!m_anchors.contains(destination)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + destination);
        }

        if (edge == Edge::Center) {
            if (const OpResult locked = contentLockFailure(destination, origin); !locked.ok) {
                return locked;
            }
        } else if (const OpResult locked = layoutLockFailure(destination, QStringLiteral("nothing can be split beside it")); !locked.ok) {
            return locked;
        }

        ViewRecord copy = m_views.value(viewId);
        copy.id = newId(QStringLiteral("v"));
        m_views.insert(copy.id, copy);

        if (edge == Edge::Center) {
            const AnchorRecord &target = m_anchors[destination];
            const qsizetype original = target.views.indexOf(viewId);
            insertViewIntoAnchor(copy.id, destination, original < 0 ? -1 : int(original + 1), true);
        } else {
            AnchorRecord created;
            created.id = newId(QStringLiteral("a"));
            m_anchors.insert(created.id, created);
            insertBeside(leafOfAnchor(destination), createLeaf(created.id), edge);
            insertViewIntoAnchor(copy.id, created.id, -1, true);
            m_focusedAnchorId = created.id;
        }

        bump();
        return OpResult::success(copy.id);
    });
}

OpResult WorkspaceModel::moveView(QString viewId, QString targetAnchorId, int index, Origin origin)
{
    return transact([&]() {
        if (!m_views.contains(viewId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }

        if (!m_anchors.contains(targetAnchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + targetAnchorId);
        }

        const QString sourceAnchor = m_anchorOfView.value(viewId);

        // Reordering is navigation; moving between anchors changes content.
        if (sourceAnchor != targetAnchorId) {
            for (const QString &affected : {sourceAnchor, targetAnchorId}) {
                if (const OpResult locked = contentLockFailure(affected, origin); !locked.ok) {
                    return locked;
                }
            }
        }

        if (sourceAnchor == targetAnchorId) {
            AnchorRecord &record = m_anchors[sourceAnchor];
            record.views.removeAll(viewId);
            const qsizetype position = index < 0
                ? record.views.size()
                : std::clamp<qsizetype>(index, 0, record.views.size());
            record.views.insert(position, viewId);
            record.activeViewId = viewId;
            m_focusedAnchorId = sourceAnchor;
            bump();
            return OpResult::success();
        }

        removeViewFromAnchor(viewId);
        insertViewIntoAnchor(viewId, targetAnchorId, index < 0 ? int(m_anchors[targetAnchorId].views.size()) : index, true);
        m_focusedAnchorId = targetAnchorId;
        collapseIfIncidental(sourceAnchor);
        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::splitView(QString viewId, QString targetAnchorId, Edge edge, Origin origin)
{
    if (edge == Edge::Center) {
        return moveView(viewId, targetAnchorId, -1, origin);
    }

    return transact([&]() {
        if (!m_views.contains(viewId) || !m_anchors.contains(targetAnchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view or anchor."));
        }

        const QString sourceAnchor = m_anchorOfView.value(viewId);

        if (const OpResult locked = contentLockFailure(sourceAnchor, origin); !locked.ok) {
            return locked;
        }

        if (const OpResult locked = layoutLockFailure(targetAnchorId, QStringLiteral("nothing can be split beside it")); !locked.ok) {
            return locked;
        }

        if (sourceAnchor == targetAnchorId && m_anchors.value(sourceAnchor).views.size() == 1) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("A tab can't be split away from itself."));
        }

        AnchorRecord created;
        created.id = newId(QStringLiteral("a"));
        m_anchors.insert(created.id, created);
        insertBeside(leafOfAnchor(targetAnchorId), createLeaf(created.id), edge);
        removeViewFromAnchor(viewId);
        insertViewIntoAnchor(viewId, created.id, -1, true);
        m_focusedAnchorId = created.id;
        collapseIfIncidental(sourceAnchor);
        bump();
        return OpResult::success(created.id);
    });
}

OpResult WorkspaceModel::dockViewAtSurfaceEdge(QString viewId, QString surfaceId, Edge edge, Origin origin)
{
    return transact([&]() {
        if (!m_views.contains(viewId) || !m_surfaces.contains(surfaceId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view or window."));
        }

        if (edge == Edge::Center) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Pick a window edge."));
        }

        const QString sourceAnchor = m_anchorOfView.value(viewId);

        if (const OpResult locked = contentLockFailure(sourceAnchor, origin); !locked.ok) {
            return locked;
        }

        if (const OpResult locked = surfaceEdgeLockFailure(surfaceId, edge); !locked.ok) {
            return locked;
        }
        const QStringList surfaceAnchors = anchorIds(surfaceId);

        if (surfaceAnchors == QStringList{sourceAnchor} && m_anchors.value(sourceAnchor).views.size() == 1) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("This tab already fills the window."));
        }

        AnchorRecord created;
        created.id = newId(QStringLiteral("a"));
        m_anchors.insert(created.id, created);
        insertAtSurfaceEdge(surfaceId, createLeaf(created.id), edge);
        removeViewFromAnchor(viewId);
        insertViewIntoAnchor(viewId, created.id, -1, true);
        m_focusedAnchorId = created.id;
        collapseIfIncidental(sourceAnchor);
        bump();
        return OpResult::success(created.id);
    });
}

OpResult WorkspaceModel::detachView(QString viewId, const QRectF &geometry, QString screenName, Origin origin, bool inMainWindow)
{
    return transact([&]() {
        if (!m_views.contains(viewId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }

        const QString sourceAnchor = m_anchorOfView.value(viewId);

        if (const OpResult locked = contentLockFailure(sourceAnchor, origin); !locked.ok) {
            return locked;
        }

        // Already alone in a floating surface: that is just moving it.
        const QString sourceSurface = surfaceOfAnchor(sourceAnchor);
        if (!m_surfaces.value(sourceSurface).primary && anchorIds(sourceSurface).size() == 1
            && m_anchors.value(sourceAnchor).views.size() == 1) {
            placeSurface(sourceSurface, geometry, screenName, inMainWindow);
            m_focusedAnchorId = sourceAnchor;
            bump();
            return OpResult::success(sourceSurface);
        }

        const QString surfaceId = createSurface(geometry, screenName, inMainWindow);
        AnchorRecord created;
        created.id = newId(QStringLiteral("a"));
        m_anchors.insert(created.id, created);
        m_surfaces[surfaceId].rootNodeId = createLeaf(created.id);
        removeViewFromAnchor(viewId);
        insertViewIntoAnchor(viewId, created.id, -1, true);
        m_focusedAnchorId = created.id;
        collapseIfIncidental(sourceAnchor);
        bump();
        return OpResult::success(surfaceId);
    });
}

// ---- anchors -----------------------------------------------------------------

OpResult WorkspaceModel::createAnchor(QString targetAnchorId, Edge edge)
{
    return transact([&]() {
        const QString target = targetAnchorId.isEmpty() ? this->targetAnchorId() : targetAnchorId;

        if (!m_anchors.contains(target)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + target);
        }

        if (const OpResult locked = layoutLockFailure(target, QStringLiteral("nothing can be split beside it")); !locked.ok) {
            return locked;
        }

        AnchorRecord created;
        created.id = newId(QStringLiteral("a"));
        created.pinned = true;
        m_anchors.insert(created.id, created);
        insertBeside(leafOfAnchor(target), createLeaf(created.id), edge == Edge::Center ? Edge::Right : edge);
        m_focusedAnchorId = created.id;
        bump();
        return OpResult::success(created.id);
    });
}

OpResult WorkspaceModel::createAnchorAtSurfaceEdge(QString surfaceId, Edge edge)
{
    return transact([&]() {
        if (!m_surfaces.contains(surfaceId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
        }

        if (edge == Edge::Center) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Pick a window edge."));
        }

        if (const OpResult locked = surfaceEdgeLockFailure(surfaceId, edge); !locked.ok) {
            return locked;
        }

        AnchorRecord created;
        created.id = newId(QStringLiteral("a"));
        created.pinned = true;
        m_anchors.insert(created.id, created);
        insertAtSurfaceEdge(surfaceId, createLeaf(created.id), edge);
        m_focusedAnchorId = created.id;
        bump();
        return OpResult::success(created.id);
    });
}

OpResult WorkspaceModel::closeAnchor(QString anchorId, Origin origin)
{
    return transact([&]() {
        if (!m_anchors.contains(anchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }

        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't be closed")); !locked.ok) {
            return locked;
        }

        if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
            return locked;
        }

        for (const QString &viewId : m_anchors.value(anchorId).views) {
            m_views.remove(viewId);
            m_anchorOfView.remove(viewId);
        }

        m_anchors[anchorId].views.clear();
        m_anchors[anchorId].activeViewId.clear();
        const QString surfaceId = surfaceOfAnchor(anchorId);

        if (!(m_surfaces.value(surfaceId).primary && anchorIds(surfaceId).size() == 1)) {
            removeAnchorRecord(anchorId);
        }

        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::mergeAnchor(QString anchorId, QString targetAnchorId, Origin origin, int index)
{
    return transact([&]() {
        if (!m_anchors.contains(anchorId) || !m_anchors.contains(targetAnchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor."));
        }

        if (anchorId == targetAnchorId) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("An anchor can't merge into itself."));
        }

        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't be merged into another panel")); !locked.ok) {
            return locked;
        }

        for (const QString &affected : {anchorId, targetAnchorId}) {
            if (const OpResult locked = contentLockFailure(affected, origin); !locked.ok) {
                return locked;
            }
        }

        const AnchorRecord source = m_anchors.value(anchorId);

        for (const QString &viewId : source.views) {
            m_anchorOfView.remove(viewId);
        }

        m_anchors[anchorId].views.clear();

        int position = index < 0 ? int(m_anchors[targetAnchorId].views.size())
                                 : std::clamp(index, 0, int(m_anchors[targetAnchorId].views.size()));
        for (const QString &viewId : source.views) {
            insertViewIntoAnchor(viewId, targetAnchorId, position, viewId == source.activeViewId);
            position += 1;
        }

        removeAnchorRecord(anchorId);
        m_focusedAnchorId = targetAnchorId;
        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::moveAnchor(QString anchorId, QString targetAnchorId, Edge edge, Origin origin)
{
    if (edge == Edge::Center) {
        return mergeAnchor(anchorId, targetAnchorId, origin);
    }

    return transact([&]() {
        if (!m_anchors.contains(anchorId) || !m_anchors.contains(targetAnchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor."));
        }

        if (anchorId == targetAnchorId) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("An anchor can't dock beside itself."));
        }

        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't be moved")); !locked.ok) {
            return locked;
        }

        if (const OpResult locked = layoutLockFailure(targetAnchorId, QStringLiteral("nothing can be docked beside it")); !locked.ok) {
            return locked;
        }

        const QString sourceSurface = surfaceOfAnchor(anchorId);
        const QString leafId = leafOfAnchor(anchorId);
        detachNode(leafId);
        insertBeside(leafOfAnchor(targetAnchorId), leafId, edge);
        dropSurfaceIfEmpty(sourceSurface);
        ensurePrimaryHasAnchor();
        m_focusedAnchorId = anchorId;
        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::dockAnchorAtSurfaceEdge(QString anchorId, QString surfaceId, Edge edge)
{
    return transact([&]() {
        if (!m_anchors.contains(anchorId) || !m_surfaces.contains(surfaceId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor or window."));
        }

        if (edge == Edge::Center) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Pick a window edge."));
        }

        if (anchorIds(surfaceId) == QStringList{anchorId}) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("This group already fills the window."));
        }

        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't be moved")); !locked.ok) {
            return locked;
        }

        const QString sourceSurface = surfaceOfAnchor(anchorId);
        const QString leafId = leafOfAnchor(anchorId);
        detachNode(leafId);

        if (const OpResult locked = surfaceEdgeLockFailure(surfaceId, edge); !locked.ok) {
            return locked;
        }

        insertAtSurfaceEdge(surfaceId, leafId, edge);
        dropSurfaceIfEmpty(sourceSurface);
        ensurePrimaryHasAnchor();
        m_focusedAnchorId = anchorId;
        bump();
        return OpResult::success();
    });
}

OpResult WorkspaceModel::detachAnchor(QString anchorId, const QRectF &geometry, QString screenName, bool inMainWindow)
{
    return transact([&]() {
        if (!m_anchors.contains(anchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }

        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't move to another window")); !locked.ok) {
            return locked;
        }

        const QString sourceSurface = surfaceOfAnchor(anchorId);

        // Already alone in a floating window: that is just moving the window.
        if (!m_surfaces.value(sourceSurface).primary && anchorIds(sourceSurface).size() == 1) {
            placeSurface(sourceSurface, geometry, screenName, inMainWindow);
            bump();
            return OpResult::success(sourceSurface);
        }

        const QString leafId = leafOfAnchor(anchorId);
        detachNode(leafId);
        const QString surfaceId = createSurface(geometry, screenName, inMainWindow);
        m_surfaces[surfaceId].rootNodeId = leafId;
        ensurePrimaryHasAnchor();
        m_focusedAnchorId = anchorId;
        bump();
        return OpResult::success(surfaceId);
    });
}

OpResult WorkspaceModel::setAnchorPinned(QString anchorId, bool pinned)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    m_anchors[anchorId].pinned = pinned;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::setAnchorRoles(QString anchorId, const QStringList &roles)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    m_anchors[anchorId].roles = roles;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::focusAnchor(QString anchorId)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    if (m_focusedAnchorId != anchorId) {
        m_focusedAnchorId = anchorId;
        bump();
    }

    return OpResult::success();
}

OpResult WorkspaceModel::setTargetAnchor(QString anchorId)
{
    if (!anchorId.isEmpty() && !m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    m_targetAnchorId = anchorId;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::maximizeAnchor(QString anchorId)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    m_surfaces[surfaceOfAnchor(anchorId)].maximizedAnchorId = anchorId;
    m_focusedAnchorId = anchorId;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::restoreAnchor(QString surfaceId)
{
    if (!m_surfaces.contains(surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
    }

    m_surfaces[surfaceId].maximizedAnchorId.clear();
    bump();
    return OpResult::success();
}

// ---- anchor identity and configuration ---------------------------------------

namespace
{
constexpr int kMaximumAnchorNameLength = 80;

OpResult unlockRefused(Origin origin)
{
    return OpResult::failure(
        QStringLiteral("UNLOCK_REFUSED"),
        origin == Origin::Agent
            ? QStringLiteral("Only you can unlock a panel; an agent can't.")
            : QStringLiteral("A panel is only unlocked when you ask for it.")
    );
}

QString baseNameForType(const QString &typeId)
{
    if (typeId == QStringLiteral("archivist.editor") || typeId == QStringLiteral("archivist.preview")) {
        return QStringLiteral("Editor");
    }
    if (typeId == QStringLiteral("archivist.chat")) {
        return QStringLiteral("Chat");
    }
    if (typeId == QStringLiteral("archivist.terminal")) {
        return QStringLiteral("Terminal");
    }
    if (typeId == QStringLiteral("archivist.diff")) {
        return QStringLiteral("Review");
    }
    return QStringLiteral("Panel");
}
} // namespace

OpResult WorkspaceModel::renameAnchor(QString anchorId, QString name, Origin origin)
{
    Q_UNUSED(origin)

    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    // One line, trimmed, bounded. Blank resets to the automatic name.
    QString clean = name.simplified();
    if (clean.size() > kMaximumAnchorNameLength) {
        clean = clean.left(kMaximumAnchorNameLength).trimmed();
    }

    AnchorRecord &record = m_anchors[anchorId];
    if (record.name != clean) {
        record.name = clean;
        bump();
    }
    return OpResult::success();
}

OpResult WorkspaceModel::setAnchorLayoutLocked(QString anchorId, bool locked, Origin origin)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    AnchorRecord &record = m_anchors[anchorId];

    if (!locked && record.layoutLocked && origin != Origin::User) {
        return unlockRefused(origin);
    }

    if (record.layoutLocked != locked) {
        record.layoutLocked = locked;
        bump();
    }
    return OpResult::success();
}

OpResult WorkspaceModel::setAnchorContentLocked(QString anchorId, bool locked, Origin origin)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    AnchorRecord &record = m_anchors[anchorId];

    if (!locked && record.contentLocked && origin != Origin::User) {
        return unlockRefused(origin);
    }

    if (record.contentLocked != locked) {
        record.contentLocked = locked;
        bump();
    }
    return OpResult::success();
}

OpResult WorkspaceModel::setAnchorSwitchable(QString anchorId, bool switchable, Origin origin)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    // How the views are presented, not which views: allowed under a content
    // lock for the user, refused for automation.
    if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
        return locked;
    }

    AnchorRecord &record = m_anchors[anchorId];
    if (record.switchable != switchable) {
        record.switchable = switchable;
        bump();
    }
    return OpResult::success();
}

OpResult WorkspaceModel::setAnchorLibrary(QString anchorId, QString libraryId, Origin origin)
{
    if (!m_anchors.contains(anchorId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
    }

    AnchorRecord &record = m_anchors[anchorId];

    // Global worktree switches never redirect a panel the user configured.
    if (origin == Origin::Automatic && (record.pinned || record.layoutLocked || record.contentLocked)) {
        return OpResult::failure(
            QStringLiteral("CONTENT_LOCKED"),
            QStringLiteral("“%1” keeps its own worktree.").arg(anchorDisplayName(anchorId))
        );
    }

    if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
        return locked;
    }

    if (record.libraryId != libraryId) {
        record.libraryId = libraryId;
        bump();
    }
    return OpResult::success();
}

QString WorkspaceModel::anchorDisplayName(const QString &anchorId) const
{
    const auto it = m_anchors.constFind(anchorId);
    if (it == m_anchors.constEnd()) {
        return {};
    }
    return it->name.isEmpty() ? automaticAnchorName(anchorId) : it->name;
}

QString WorkspaceModel::automaticAnchorName(const QString &anchorId) const
{
    // "Editor", "Chat", "Terminal" from what the anchor holds (or prefers),
    // numbered when several anchors would share a name.
    auto baseName = [this](const AnchorRecord &record) {
        if (record.switchable) {
            return QStringLiteral("Shelf");
        }

        QString base;
        for (const QString &viewId : record.views) {
            const QString candidate = baseNameForType(m_views.value(viewId).typeId);
            if (base.isEmpty()) {
                base = candidate;
            } else if (base != candidate) {
                return QStringLiteral("Panel");
            }
        }

        if (base.isEmpty() && !record.roles.isEmpty()) {
            base = baseNameForType(record.roles.first());
        }
        return base.isEmpty() ? QStringLiteral("Panel") : base;
    };

    const auto it = m_anchors.constFind(anchorId);
    if (it == m_anchors.constEnd()) {
        return {};
    }

    const QString base = baseName(*it);
    int number = 0;
    int total = 0;

    // Anchors that hold something are numbered first, so an empty panel
    // waiting for a terminal doesn't turn the only real one into "Terminal 2".
    for (const bool holdingViews : {true, false}) {
        for (const QString &other : anchorIds()) {
            const AnchorRecord &record = m_anchors[other];
            if (record.views.isEmpty() == holdingViews || !record.name.isEmpty() || baseName(record) != base) {
                continue;
            }
            ++total;
            if (other == anchorId) {
                number = total;
            }
        }
    }

    return number > 1 ? QStringLiteral("%1 %2").arg(base).arg(number) : base;
}

AnchorCapabilities WorkspaceModel::anchorCapabilities(const QString &anchorId, Origin origin) const
{
    AnchorCapabilities caps;
    const auto it = m_anchors.constFind(anchorId);

    if (it == m_anchors.constEnd()) {
        caps.canMove = caps.canClose = caps.canResize = caps.canMerge = caps.canFloat = caps.canDockBeside = false;
        caps.canAddViews = caps.canRemoveViews = caps.canReplaceContent = caps.canRename = false;
        caps.canUnlock = caps.canSetWorktree = false;
        caps.reasons.insert(QStringLiteral("all"), QStringLiteral("No such anchor."));
        return caps;
    }

    if (it->layoutLocked) {
        const QString reason = layoutLockFailure(anchorId, QStringLiteral("its place is fixed")).message;
        caps.canMove = caps.canClose = caps.canResize = caps.canMerge = caps.canFloat = caps.canDockBeside = false;
        for (const char *key : {"move", "close", "resize", "merge", "float", "dockBeside"}) {
            caps.reasons.insert(QString::fromLatin1(key), reason);
        }
    }

    const OpResult content = contentLockFailure(anchorId, origin);
    if (!content.ok) {
        caps.canAddViews = caps.canRemoveViews = caps.canReplaceContent = caps.canClose = caps.canMerge = false;
        caps.canSetWorktree = false;
        for (const char *key : {"addViews", "removeViews", "replaceContent", "setWorktree"}) {
            caps.reasons.insert(QString::fromLatin1(key), content.message);
        }
        if (!caps.reasons.contains(QStringLiteral("close"))) {
            caps.reasons.insert(QStringLiteral("close"), content.message);
            caps.reasons.insert(QStringLiteral("merge"), content.message);
        }
    }

    if (origin == Origin::Automatic && (it->pinned || it->layoutLocked || it->contentLocked)) {
        caps.canSetWorktree = false;
        caps.reasons.insert(QStringLiteral("setWorktree"), QStringLiteral("This panel keeps its own worktree."));
    }

    caps.canUnlock = origin == Origin::User;
    if (!caps.canUnlock) {
        caps.reasons.insert(QStringLiteral("unlock"), unlockRefused(origin).message);
    }

    return caps;
}

LibraryResolution WorkspaceModel::resolveLibrary(
    const QString &anchorId,
    const QString &explorerLibraryId,
    const QString &defaultLibraryId,
    const QStringList &knownLibraryIds
) const
{
    auto usable = [&](const QString &libraryId) {
        return !libraryId.isEmpty() && (knownLibraryIds.isEmpty() || knownLibraryIds.contains(libraryId));
    };

    LibraryResolution result;
    const auto anchor = m_anchors.constFind(anchorId);

    if (anchor != m_anchors.constEnd()) {
        if (usable(anchor->libraryId)) {
            result.libraryId = anchor->libraryId;
            result.source = LibraryResolution::Source::AnchorBinding;
            return result;
        }

        const auto active = m_views.constFind(anchor->activeViewId);
        if (active != m_views.constEnd() && usable(active->resource.libraryId)) {
            result.libraryId = active->resource.libraryId;
            result.source = LibraryResolution::Source::ActiveView;
            return result;
        }
    }

    if (usable(explorerLibraryId)) {
        result.libraryId = explorerLibraryId;
        result.source = LibraryResolution::Source::Explorer;
        return result;
    }

    if (usable(defaultLibraryId)) {
        result.libraryId = defaultLibraryId;
        result.source = LibraryResolution::Source::Default;
    }
    return result;
}

// ---- splits ------------------------------------------------------------------

OpResult WorkspaceModel::setSplitSizes(QString nodeId, const QList<double> &sizes)
{
    if (!m_nodes.contains(nodeId) || m_nodes.value(nodeId).leaf) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such split: ") + nodeId);
    }

    if (sizes.size() != m_nodes.value(nodeId).children.size()) {
        return OpResult::failure(QStringLiteral("INVALID_SIZES"), QStringLiteral("One size per child is needed."));
    }

    for (double size : sizes) {
        if (!std::isfinite(size) || size <= 0) {
            return OpResult::failure(QStringLiteral("INVALID_SIZES"), QStringLiteral("Sizes must be positive."));
        }
    }

    const SplitNode &split = m_nodes[nodeId];
    const QList<double> next = normalized(sizes);

    for (qsizetype i = 0; i < split.children.size(); ++i) {
        if (std::abs(next.at(i) - split.sizes.value(i)) > 1e-6 && containsLayoutLocked(split.children.at(i))) {
            QStringList anchors;
            collectAnchors(split.children.at(i), anchors);
            for (const QString &anchorId : anchors) {
                if (isLayoutLocked(anchorId)) {
                    return layoutLockFailure(anchorId, QStringLiteral("it can't be resized"));
                }
            }
        }
    }

    m_nodes[nodeId].sizes = next;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::equalizeSplit(QString nodeId)
{
    if (!m_nodes.contains(nodeId) || m_nodes.value(nodeId).leaf) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such split: ") + nodeId);
    }

    // Locked panels keep their size; the others share what is left evenly.
    const SplitNode split = m_nodes.value(nodeId);
    double unlockedTotal = 0;
    int unlockedCount = 0;

    for (qsizetype i = 0; i < split.children.size(); ++i) {
        if (!containsLayoutLocked(split.children.at(i))) {
            unlockedTotal += split.sizes.value(i);
            ++unlockedCount;
        }
    }

    if (unlockedCount == 0) {
        return setSplitSizes(nodeId, QList<double>(split.children.size(), 1.0 / split.children.size()));
    }

    QList<double> sizes = split.sizes;
    for (qsizetype i = 0; i < split.children.size(); ++i) {
        if (!containsLayoutLocked(split.children.at(i))) {
            sizes[i] = unlockedTotal / unlockedCount;
        }
    }
    return setSplitSizes(nodeId, sizes);
}

OpResult WorkspaceModel::resizeSplit(
    QString surfaceId,
    QString nodeId,
    int index,
    double position,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics
)
{
    const SplitNode split = m_nodes.value(nodeId);

    if (!m_nodes.contains(nodeId) || split.leaf || index < 0 || index + 1 >= split.children.size()) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such splitter."));
    }

    if (!std::isfinite(position)) {
        return OpResult::failure(QStringLiteral("INVALID_SIZES"), QStringLiteral("Bad splitter position."));
    }

    const LayoutGeometry geometry = computeGeometry(surfaceId, surfaceSize, metrics);
    const QRectF rect = geometry.nodes.value(nodeId);

    if (!rect.isValid()) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("The split is not on screen."));
    }

    const bool horizontal = split.orientation == Orientation::Horizontal;
    const qsizetype count = split.children.size();
    const double start = horizontal ? rect.left() : rect.top();
    const double available = std::max(1.0, (horizontal ? rect.width() : rect.height()) - metrics.splitterThickness * (count - 1));

    // Where children[index] starts, in pixels, given the current fractions.
    double before = 0;
    for (int i = 0; i < index; ++i) {
        before += split.sizes.value(i) * available;
    }

    const double pair = (split.sizes.value(index) + split.sizes.value(index + 1)) * available;
    const QSizeF minimumA = minimumSize(split.children.at(index), metrics);
    const QSizeF minimumB = minimumSize(split.children.at(index + 1), metrics);
    const double minA = horizontal ? minimumA.width() : minimumA.height();
    const double minB = horizontal ? minimumB.width() : minimumB.height();
    const double offset = position - start - before - index * metrics.splitterThickness;
    double first = std::clamp(offset, std::min(minA, pair / 2), std::max(pair - minB, pair / 2));

    if (pair < minA + minB) {
        first = pair * minA / std::max(1.0, minA + minB);
    }

    QList<double> sizes = split.sizes;
    const double total = split.sizes.value(index) + split.sizes.value(index + 1);
    sizes[index] = std::max(kMinimumFraction, total * first / std::max(1.0, pair));
    sizes[index + 1] = std::max(kMinimumFraction, total - sizes[index]);
    return setSplitSizes(nodeId, sizes);
}

// ---- surfaces ----------------------------------------------------------------

OpResult WorkspaceModel::setSurfaceGeometry(QString surfaceId, const QRectF &geometry, QString screenName, bool userPlaced)
{
    if (!m_surfaces.contains(surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
    }

    SurfaceRecord &record = m_surfaces[surfaceId];
    record.geometry = geometry;
    record.screenName = screenName;

    if (userPlaced) {
        record.preferredGeometry = geometry;
        record.preferredScreenName = screenName;
    }

    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::setSurfaceSidebarSlot(QString surfaceId, QString slot)
{
    if (!m_surfaces.contains(surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
    }
    SurfaceRecord &record = m_surfaces[surfaceId];
    if (record.primary) {
        return OpResult::failure(QStringLiteral("PRIMARY_SURFACE"), QStringLiteral("The main window can't go in the sidebar."));
    }
    const QString holder = surfaceInSidebarSlot(slot);
    if (!slot.isEmpty() && !holder.isEmpty() && holder != surfaceId) {
        return OpResult::failure(QStringLiteral("SLOT_TAKEN"), QStringLiteral("Something is already docked there."));
    }
    record.sidebarSlot = slot;
    record.inMainWindow = slot.isEmpty();
    record.screenName.clear();
    record.preferredScreenName.clear();
    record.windowMaximized = false;
    bump();
    return OpResult::success(surfaceId);
}

OpResult WorkspaceModel::setSurfaceLeaveOnTop(QString surfaceId, bool leaveOnTop)
{
    if (!m_surfaces.contains(surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
    }
    SurfaceRecord &record = m_surfaces[surfaceId];
    if (record.leaveOnTop == leaveOnTop) {
        return OpResult::success(surfaceId);
    }
    record.leaveOnTop = leaveOnTop;
    bump();
    return OpResult::success(surfaceId);
}

QString WorkspaceModel::surfaceInSidebarSlot(const QString &slot) const
{
    if (slot.isEmpty()) {
        return {};
    }
    for (const QString &surfaceId : m_surfaceOrder) {
        if (m_surfaces.value(surfaceId).sidebarSlot == slot) {
            return surfaceId;
        }
    }
    return {};
}

OpResult WorkspaceModel::setSurfaceWindowMaximized(QString surfaceId, bool maximized)
{
    if (!m_surfaces.contains(surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
    }

    m_surfaces[surfaceId].windowMaximized = maximized;
    bump();
    return OpResult::success();
}

OpResult WorkspaceModel::closeSurface(QString surfaceId)
{
    return transact([&]() {
        if (!m_surfaces.contains(surfaceId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window: ") + surfaceId);
        }

        if (m_surfaces.value(surfaceId).primary) {
            return OpResult::failure(QStringLiteral("PRIMARY_SURFACE"), QStringLiteral("The main window can't be closed this way."));
        }

        for (const QString &anchorId : anchorIds(surfaceId)) {
            if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("its window can't be closed")); !locked.ok) {
                return locked;
            }
        }

        const QString primaryId = primarySurfaceId();
        QString destination = targetAnchorId();

        if (surfaceOfAnchor(destination) != primaryId) {
            destination = m_anchors.contains(m_focusedAnchorId) && surfaceOfAnchor(m_focusedAnchorId) == primaryId
                ? m_focusedAnchorId
                : anchorIds(primaryId).value(0);
        }

        for (const QString &anchorId : anchorIds(surfaceId)) {
            const AnchorRecord source = m_anchors.value(anchorId);

            for (const QString &viewId : source.views) {
                m_anchorOfView.remove(viewId);
                insertViewIntoAnchor(viewId, destination, int(m_anchors[destination].views.size()), false);
            }

            m_anchors[anchorId].views.clear();
            removeAnchorRecord(anchorId);
        }

        m_surfaces.remove(surfaceId);
        m_surfaceOrder.removeAll(surfaceId);

        if (!m_anchors.contains(m_focusedAnchorId)) {
            m_focusedAnchorId = destination;
        }

        bump();
        return OpResult::success();
    });
}

QStringList WorkspaceModel::recoverSurfaces(const QList<ScreenInfo> &screens)
{
    QStringList moved;

    if (screens.isEmpty()) {
        return moved;
    }

    auto screenNamed = [&](const QString &name) -> const ScreenInfo * {
        for (const ScreenInfo &screen : screens) {
            if (!name.isEmpty() && screen.name == name) {
                return &screen;
            }
        }
        return nullptr;
    };

    const ScreenInfo *primaryScreen = &screens.first();
    for (const ScreenInfo &screen : screens) {
        if (screen.primary) {
            primaryScreen = &screen;
            break;
        }
    }

    for (const QString &surfaceId : m_surfaceOrder) {
        SurfaceRecord &record = m_surfaces[surfaceId];

        // Panels inside the main window, and the sidebar, go wherever it goes.
        if (!record.geometry.isValid() || record.inMainWindow || !record.sidebarSlot.isEmpty()) {
            continue;
        }

        // The monitor the user chose is back: return there.
        const ScreenInfo *preferred = screenNamed(record.preferredScreenName);
        if (preferred && record.screenName != record.preferredScreenName && record.preferredGeometry.isValid()
            && preferred->geometry.intersects(record.preferredGeometry)) {
            record.geometry = record.preferredGeometry;
            record.screenName = record.preferredScreenName;
            moved.append(surfaceId);
            continue;
        }

        // Still visible enough where it is: leave it.
        const ScreenInfo *current = screenNamed(record.screenName);
        double bestOverlap = 0;
        const ScreenInfo *best = nullptr;

        for (const ScreenInfo &screen : screens) {
            const double overlap = area(screen.geometry.intersected(record.geometry));
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                best = &screen;
            }
        }

        const bool visible = bestOverlap >= 0.25 * area(record.geometry);

        if (current && visible) {
            continue;
        }

        if (!current && visible && best) {
            record.screenName = best->name;
            moved.append(surfaceId);
            continue;
        }

        // Gone: centre it on the primary screen, shrunk to fit.
        const QRectF available = primaryScreen->availableGeometry.isValid()
            ? primaryScreen->availableGeometry
            : primaryScreen->geometry;
        const double width = std::min(record.geometry.width(), available.width() * 0.9);
        const double height = std::min(record.geometry.height(), available.height() * 0.9);
        record.geometry = QRectF(
            available.x() + (available.width() - width) / 2.0,
            available.y() + (available.height() - height) / 2.0,
            width,
            height
        );
        record.screenName = primaryScreen->name;
        moved.append(surfaceId);
    }

    if (!moved.isEmpty()) {
        bump();
    }

    return moved;
}

// ---- drag and drop -------------------------------------------------------

OpResult WorkspaceModel::canDrop(const DragSource &source, const DropTarget &target) const
{
    if (!m_surfaces.contains(target.surfaceId)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such window."));
    }

    if (!target.anchorId.isEmpty()) {
        if (!m_anchors.contains(target.anchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor."));
        }
        if (surfaceOfAnchor(target.anchorId) != target.surfaceId) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("That anchor is in another window."));
        }
    } else if (target.edge == Edge::Center) {
        return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Nothing to drop onto."));
    }

    const QStringList surfaceAnchors = anchorIds(target.surfaceId);

    switch (source.kind) {
    case DragSource::Kind::None:
        return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Nothing is being dragged."));

    case DragSource::Kind::View: {
        if (!m_views.contains(source.id)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view."));
        }
        const QString sourceAnchor = m_anchorOfView.value(source.id);
        const bool alone = m_anchors.value(sourceAnchor).views.size() == 1;

        if (target.isSurfaceEdge()) {
            if (alone && surfaceAnchors == QStringList{sourceAnchor}) {
                return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("This tab already fills the window."));
            }
            return surfaceEdgeLockFailure(target.surfaceId, target.edge);
        }

        if (target.edge != Edge::Center && sourceAnchor == target.anchorId && alone) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("A tab can't be split away from itself."));
        }
        if (target.edge != Edge::Center) {
            return layoutLockFailure(target.anchorId, QStringLiteral("nothing can be split beside it"));
        }
        return OpResult::success();
    }

    case DragSource::Kind::Anchor: {
        if (!m_anchors.contains(source.id)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor."));
        }

        if (const OpResult locked = layoutLockFailure(source.id, QStringLiteral("it can't be moved")); !locked.ok) {
            return locked;
        }

        if (target.isSurfaceEdge()) {
            if (surfaceAnchors == QStringList{source.id}) {
                return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("This group already fills the window."));
            }
            // The panel leaves its place first, so judge the window without it.
            WorkspaceModel after = *this;
            after.detachNode(after.leafOfAnchor(source.id));
            return after.surfaceEdgeLockFailure(target.surfaceId, target.edge);
        }

        if (target.anchorId == source.id) {
            return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("A group can't dock onto itself."));
        }
        if (target.edge != Edge::Center) {
            return layoutLockFailure(target.anchorId, QStringLiteral("nothing can be docked beside it"));
        }
        return OpResult::success();
    }
    }

    return OpResult::failure(QStringLiteral("INVALID_DROP"), QStringLiteral("Unknown drag."));
}

OpResult WorkspaceModel::applyDrop(const DragSource &source, const DropTarget &target)
{
    const OpResult allowed = canDrop(source, target);

    if (!allowed.ok) {
        return allowed;
    }

    if (source.kind == DragSource::Kind::View) {
        if (target.isSurfaceEdge()) {
            return dockViewAtSurfaceEdge(source.id, target.surfaceId, target.edge);
        }
        if (target.edge == Edge::Center) {
            // A drop's index is a slot between the tabs as they are now (as
            // the caret shows it). Within its own panel the tab leaves its
            // slot first, so a slot after it is one less.
            int index = target.index;
            if (index >= 0 && m_anchorOfView.value(source.id) == target.anchorId) {
                const int current = int(m_anchors.value(target.anchorId).views.indexOf(source.id));
                if (current >= 0 && index > current) {
                    index -= 1;
                }
            }
            return moveView(source.id, target.anchorId, index);
        }
        return splitView(source.id, target.anchorId, target.edge);
    }

    if (target.isSurfaceEdge()) {
        return dockAnchorAtSurfaceEdge(source.id, target.surfaceId, target.edge);
    }

    if (target.edge == Edge::Center) {
        return mergeAnchor(source.id, target.anchorId, Origin::User, target.index);
    }

    return moveAnchor(source.id, target.anchorId, target.edge);
}

LayoutGeometry WorkspaceModel::previewDrop(
    const DragSource &source,
    const DropTarget &target,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics,
    QRectF *landingRect
) const
{
    WorkspaceModel after = *this;
    const OpResult result = after.applyDrop(source, target);

    if (!result.ok) {
        if (landingRect) {
            *landingRect = QRectF();
        }
        return computeGeometry(target.surfaceId, surfaceSize, metrics);
    }

    const LayoutGeometry geometry = after.computeGeometry(target.surfaceId, surfaceSize, metrics);

    if (landingRect) {
        const QString landing = source.kind == DragSource::Kind::View
            ? after.anchorOfView(source.id)
            : (target.edge == Edge::Center && !target.anchorId.isEmpty() ? target.anchorId : source.id);
        *landingRect = geometry.anchors.value(landing);
    }

    return geometry;
}

DropTarget WorkspaceModel::dropTargetAt(
    const QString &surfaceId,
    const QPointF &point,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics,
    double surfaceEdgeBand
) const
{
    DropTarget target;

    if (!m_surfaces.contains(surfaceId) || !QRectF(QPointF(0, 0), surfaceSize).contains(point)) {
        return target;
    }

    target.surfaceId = surfaceId;

    // A thin band along the window border docks along the whole side.
    const double left = point.x();
    const double right = surfaceSize.width() - point.x();
    const double top = point.y();
    const double bottom = surfaceSize.height() - point.y();
    const double nearest = std::min({left, right, top, bottom});

    if (anchorIds(surfaceId).size() > 1 && nearest <= surfaceEdgeBand) {
        target.edge = nearest == left ? Edge::Left : nearest == right ? Edge::Right : nearest == top ? Edge::Top : Edge::Bottom;
        return target;
    }

    const LayoutGeometry geometry = computeGeometry(surfaceId, surfaceSize, metrics);

    for (auto it = geometry.anchors.constBegin(); it != geometry.anchors.constEnd(); ++it) {
        const QRectF &rect = it.value();

        if (!rect.contains(point)) {
            continue;
        }

        target.anchorId = it.key();
        const double fx = (point.x() - rect.left()) / std::max(1.0, rect.width());
        const double fy = (point.y() - rect.top()) / std::max(1.0, rect.height());

        if (std::abs(fx - 0.5) < 0.25 && std::abs(fy - 0.5) < 0.25) {
            target.edge = Edge::Center;
            return target;
        }

        const double toLeft = fx;
        const double toRight = 1 - fx;
        const double toTop = fy;
        const double toBottom = 1 - fy;
        const double closest = std::min({toLeft, toRight, toTop, toBottom});
        target.edge = closest == toLeft ? Edge::Left : closest == toRight ? Edge::Right : closest == toTop ? Edge::Top : Edge::Bottom;
        return target;
    }

    // Over a splitter: no target.
    return DropTarget();
}

// ---- geometry ------------------------------------------------------------

QSizeF WorkspaceModel::minimumSize(const QString &nodeId, const LayoutMetrics &metrics) const
{
    const SplitNode current = m_nodes.value(nodeId);

    if (current.leaf) {
        return {metrics.minimumAnchorWidth, metrics.minimumAnchorHeight};
    }

    double along = metrics.splitterThickness * std::max<qsizetype>(0, current.children.size() - 1);
    double across = 0;

    for (const QString &child : current.children) {
        const QSizeF size = minimumSize(child, metrics);
        if (current.orientation == Orientation::Horizontal) {
            along += size.width();
            across = std::max(across, size.height());
        } else {
            along += size.height();
            across = std::max(across, size.width());
        }
    }

    return current.orientation == Orientation::Horizontal ? QSizeF(along, across) : QSizeF(across, along);
}

void WorkspaceModel::layoutNode(const QString &nodeId, const QRectF &rect, const LayoutMetrics &metrics, LayoutGeometry &out) const
{
    const SplitNode current = m_nodes.value(nodeId);

    if (current.leaf) {
        out.anchors.insert(current.anchorId, rect);
        return;
    }

    out.nodes.insert(nodeId, rect);
    const bool horizontal = current.orientation == Orientation::Horizontal;
    const qsizetype count = current.children.size();
    const double extent = horizontal ? rect.width() : rect.height();
    const double available = std::max(0.0, extent - metrics.splitterThickness * (count - 1));

    QList<double> minimums;
    double minimumTotal = 0;

    for (const QString &child : current.children) {
        const QSizeF size = minimumSize(child, metrics);
        minimums.append(horizontal ? size.width() : size.height());
        minimumTotal += minimums.last();
    }

    QList<double> extents(count, 0);

    if (minimumTotal >= available) {
        // Too small for everyone's minimum: shrink proportionally (compact).
        for (qsizetype i = 0; i < count; ++i) {
            extents[i] = minimumTotal > 0 ? available * minimums.at(i) / minimumTotal : available / count;
        }
    } else {
        // Honour each child's minimum, share the rest by the stored fractions.
        QList<bool> fixed(count, false);
        bool changed = true;

        while (changed) {
            changed = false;
            double remaining = available;
            double shareTotal = 0;

            for (qsizetype i = 0; i < count; ++i) {
                if (fixed.at(i)) {
                    remaining -= minimums.at(i);
                } else {
                    shareTotal += current.sizes.value(i, 1.0 / count);
                }
            }

            for (qsizetype i = 0; i < count; ++i) {
                if (fixed.at(i)) {
                    extents[i] = minimums.at(i);
                    continue;
                }
                extents[i] = shareTotal > 0 ? remaining * current.sizes.value(i, 1.0 / count) / shareTotal : 0;
                if (extents.at(i) < minimums.at(i)) {
                    fixed[i] = true;
                    changed = true;
                }
            }
        }
    }

    double cursor = horizontal ? rect.left() : rect.top();

    for (qsizetype i = 0; i < count; ++i) {
        const QRectF childRect = horizontal
            ? QRectF(cursor, rect.top(), extents.at(i), rect.height())
            : QRectF(rect.left(), cursor, rect.width(), extents.at(i));
        layoutNode(current.children.at(i), childRect, metrics, out);
        cursor += extents.at(i);

        if (i + 1 < count) {
            SplitterHandle handle;
            handle.nodeId = nodeId;
            handle.index = int(i);
            handle.orientation = current.orientation;
            handle.rect = horizontal
                ? QRectF(cursor, rect.top(), metrics.splitterThickness, rect.height())
                : QRectF(rect.left(), cursor, rect.width(), metrics.splitterThickness);
            out.splitters.append(handle);
            cursor += metrics.splitterThickness;
        }
    }
}

LayoutGeometry WorkspaceModel::computeGeometry(const QString &surfaceId, const QSizeF &size, const LayoutMetrics &metrics) const
{
    LayoutGeometry geometry;
    const SurfaceRecord record = m_surfaces.value(surfaceId);

    if (record.rootNodeId.isEmpty()) {
        return geometry;
    }

    const QRectF bounds(QPointF(0, 0), size);

    if (!record.maximizedAnchorId.isEmpty() && m_anchors.contains(record.maximizedAnchorId)) {
        geometry.anchors.insert(record.maximizedAnchorId, bounds);
        return geometry;
    }

    layoutNode(record.rootNodeId, bounds, metrics, geometry);
    return geometry;
}

QString WorkspaceModel::neighborAnchor(const QString &anchorId, Edge direction, const QSizeF &surfaceSize, const LayoutMetrics &metrics) const
{
    if (direction == Edge::Center || !m_anchors.contains(anchorId)) {
        return {};
    }

    const QString surfaceId = surfaceOfAnchor(anchorId);
    WorkspaceModel unmaximized = *this;
    unmaximized.m_surfaces[surfaceId].maximizedAnchorId.clear();
    const LayoutGeometry geometry = unmaximized.computeGeometry(surfaceId, surfaceSize, metrics);
    const QRectF source = geometry.anchors.value(anchorId);
    const double tolerance = metrics.splitterThickness + 1;

    QString best;
    double bestDistance = 0;
    double bestOverlap = -1;
    double bestStart = 0;

    for (auto it = geometry.anchors.constBegin(); it != geometry.anchors.constEnd(); ++it) {
        if (it.key() == anchorId) {
            continue;
        }

        const QRectF &rect = it.value();
        double distance = 0;
        double overlap = 0;

        switch (direction) {
        case Edge::Left:
            if (rect.right() > source.left() + tolerance) continue;
            distance = source.left() - rect.right();
            overlap = std::min(rect.bottom(), source.bottom()) - std::max(rect.top(), source.top());
            break;
        case Edge::Right:
            if (rect.left() < source.right() - tolerance) continue;
            distance = rect.left() - source.right();
            overlap = std::min(rect.bottom(), source.bottom()) - std::max(rect.top(), source.top());
            break;
        case Edge::Top:
            if (rect.bottom() > source.top() + tolerance) continue;
            distance = source.top() - rect.bottom();
            overlap = std::min(rect.right(), source.right()) - std::max(rect.left(), source.left());
            break;
        case Edge::Bottom:
            if (rect.top() < source.bottom() - tolerance) continue;
            distance = rect.top() - source.bottom();
            overlap = std::min(rect.right(), source.right()) - std::max(rect.left(), source.left());
            break;
        case Edge::Center:
            continue;
        }

        // Prefer touching neighbours that overlap most along the shared side.
        // Ties go to the one that comes first in reading order.
        const bool horizontalMove = direction == Edge::Left || direction == Edge::Right;
        const double start = horizontalMove ? rect.top() : rect.left();
        const bool sameDistance = std::abs(distance - bestDistance) <= 0.5;
        const bool sameOverlap = std::abs(overlap - bestOverlap) <= 0.5;
        const bool better = best.isEmpty()
            || distance < bestDistance - 0.5
            || (sameDistance && overlap > bestOverlap + 0.5)
            || (sameDistance && sameOverlap && start < bestStart);

        if (better) {
            best = it.key();
            bestDistance = distance;
            bestOverlap = overlap;
            bestStart = start;
        }
    }

    return best;
}

QString WorkspaceModel::cycleAnchor(const QString &anchorId, int step) const
{
    const QStringList all = anchorIds();

    if (all.isEmpty()) {
        return {};
    }

    const qsizetype index = all.indexOf(anchorId);

    if (index < 0) {
        return all.first();
    }

    const qsizetype count = all.size();
    return all.at(((index + step) % count + count) % count);
}

// ---- whole model -------------------------------------------------------------

// ---- Gravity Wells -------------------------------------------------------

void WorkspaceModel::ensureMainWell()
{
    for (const WellRecord &well : std::as_const(m_wells)) {
        if (well.id == kMainWellId) {
            return;
        }
    }
    WellRecord well;
    well.id = kMainWellId;
    m_wells.prepend(well);
}

const WellRecord *WorkspaceModel::well(const QString &wellId) const
{
    for (const WellRecord &well : m_wells) {
        if (well.id == wellId) {
            return &well;
        }
    }
    return nullptr;
}

WellRecord *WorkspaceModel::mutableWell(const QString &wellId)
{
    for (WellRecord &well : m_wells) {
        if (well.id == wellId) {
            return &well;
        }
    }
    return nullptr;
}

const WellEntry *WorkspaceModel::wellEntry(const QString &entryId) const
{
    for (const WellRecord &well : m_wells) {
        for (const WellEntry &entry : well.entries) {
            if (entry.id == entryId) {
                return &entry;
            }
        }
    }
    return nullptr;
}

WellEntry *WorkspaceModel::mutableEntry(const QString &entryId)
{
    for (WellRecord &well : m_wells) {
        for (WellEntry &entry : well.entries) {
            if (entry.id == entryId) {
                return &entry;
            }
        }
    }
    return nullptr;
}

QString WorkspaceModel::wellOfEntry(const QString &entryId) const
{
    for (const WellRecord &well : m_wells) {
        for (const WellEntry &entry : well.entries) {
            if (entry.id == entryId) {
                return well.id;
            }
        }
    }
    return {};
}

QString WorkspaceModel::entryOfMinimizedView(const QString &viewId) const
{
    if (viewId.isEmpty() || m_anchorOfView.contains(viewId)) {
        return {};
    }
    for (const WellRecord &well : m_wells) {
        for (const WellEntry &entry : well.entries) {
            for (const MinimizedView &minimized : entry.minimized) {
                if (minimized.viewId == viewId) {
                    return entry.id;
                }
            }
        }
    }
    return {};
}

const MinimizedView *WorkspaceModel::minimizedView(const QString &viewId) const
{
    const WellEntry *entry = wellEntry(entryOfMinimizedView(viewId));
    if (!entry) {
        return nullptr;
    }
    for (const MinimizedView &minimized : entry->minimized) {
        if (minimized.viewId == viewId) {
            return &minimized;
        }
    }
    return nullptr;
}

QStringList WorkspaceModel::minimizedViewIds() const
{
    QStringList result;
    for (const WellRecord &well : m_wells) {
        for (const WellEntry &entry : well.entries) {
            for (const MinimizedView &minimized : entry.minimized) {
                result.append(minimized.viewId);
            }
        }
    }
    return result;
}

QString WorkspaceModel::wellEntryForResource(const QString &wellId, const ResourceRef &resource) const
{
    const WellRecord *record = well(wellId);
    if (!record || resource.isEmpty()) {
        return {};
    }
    for (const WellEntry &entry : record->entries) {
        if (!entry.panel && entry.resource.key() == resource.key()) {
            return entry.id;
        }
    }
    return {};
}

void WorkspaceModel::dropEntryIfUnused(const QString &entryId)
{
    for (WellRecord &well : m_wells) {
        well.entries.removeIf([&](const WellEntry &entry) {
            return entry.id == entryId && !entry.pinned && entry.minimized.isEmpty();
        });
    }
}

OpResult WorkspaceModel::minimizeView(QString viewId, const Place &place, QString wellId, Origin origin)
{
    return transact([&]() {
        if (!m_views.contains(viewId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }
        if (isMinimized(viewId)) {
            return OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already put away."));
        }
        const QString anchorId = m_anchorOfView.value(viewId);
        if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
            return locked;
        }
        if (wellId.isEmpty()) {
            wellId = kMainWellId;
        }
        WellRecord *record = mutableWell(wellId);
        if (!record) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well: ") + wellId);
        }

        const ViewRecord &view = m_views[viewId];
        QString entryId = wellEntryForResource(wellId, view.resource);
        if (entryId.isEmpty()) {
            WellEntry entry;
            entry.id = newId(QStringLiteral("we"));
            entry.resource = view.resource;
            entry.typeId = view.typeId;
            entry.title = view.title;
            entryId = entry.id;
            record = mutableWell(wellId);
            record->entries.append(entry);
        }
        WellEntry *entry = mutableEntry(entryId);
        entry->title = view.title.isEmpty() ? entry->title : view.title;
        entry->minimized.append(MinimizedView{viewId, place});

        removeViewFromAnchor(viewId);
        collapseIfIncidental(anchorId);
        if (m_focusedAnchorId == anchorId && !m_anchors.contains(anchorId)) {
            m_focusedAnchorId.clear();
        }
        if (!m_targetAnchorId.isEmpty() && !m_anchors.contains(m_targetAnchorId)) {
            m_targetAnchorId.clear();
        }
        if (m_focusedAnchorId.isEmpty()) {
            m_focusedAnchorId = anchorIds(primarySurfaceId()).value(0);
        }
        bump();
        return OpResult::success(entryId);
    });
}

OpResult WorkspaceModel::minimizeAnchor(QString anchorId, const Place &place, QString wellId, Origin origin)
{
    return transact([&]() {
        if (!m_anchors.contains(anchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }
        const AnchorRecord anchor = m_anchors.value(anchorId);
        if (anchor.views.isEmpty()) {
            return OpResult::failure(QStringLiteral("EMPTY"), QStringLiteral("There's nothing in it to put away."));
        }
        if (const OpResult locked = layoutLockFailure(anchorId, QStringLiteral("it can't be minimized")); !locked.ok) {
            return locked;
        }
        if (const OpResult locked = contentLockFailure(anchorId, origin); !locked.ok) {
            return locked;
        }
        if (wellId.isEmpty()) {
            wellId = kMainWellId;
        }
        if (!mutableWell(wellId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well: ") + wellId);
        }

        WellEntry entry;
        entry.id = newId(QStringLiteral("we"));
        entry.panel = true;
        entry.title = anchorDisplayName(anchorId);
        entry.typeId = m_views.value(anchor.activeViewId).typeId;
        entry.panelInfo.anchorId = anchorId;
        entry.panelInfo.name = anchor.name;
        entry.panelInfo.displayName = entry.title;
        entry.panelInfo.activeViewId = anchor.activeViewId;
        entry.panelInfo.roles = anchor.roles;
        entry.panelInfo.libraryId = anchor.libraryId;
        entry.panelInfo.pinned = anchor.pinned;
        entry.panelInfo.contentLocked = anchor.contentLocked;
        entry.panelInfo.switchable = anchor.switchable;
        for (qsizetype index = 0; index < anchor.views.size(); ++index) {
            Place viewPlace = place;
            viewPlace.source = DragSource::view(anchor.views.at(index));
            viewPlace.anchorId = anchorId;
            viewPlace.index = int(index);
            entry.minimized.append(MinimizedView{anchor.views.at(index), viewPlace});
        }
        mutableWell(wellId)->entries.append(entry);

        for (const QString &viewId : anchor.views) {
            removeViewFromAnchor(viewId);
        }
        const QString surfaceId = surfaceOfAnchor(anchorId);
        if (!(m_surfaces.value(surfaceId).primary && anchorIds(surfaceId).size() == 1)) {
            removeAnchorRecord(anchorId);
        }
        if (!m_focusedAnchorId.isEmpty() && !m_anchors.contains(m_focusedAnchorId)) {
            m_focusedAnchorId.clear();
        }
        if (!m_targetAnchorId.isEmpty() && !m_anchors.contains(m_targetAnchorId)) {
            m_targetAnchorId.clear();
        }
        if (m_focusedAnchorId.isEmpty() || m_focusedAnchorId == anchorId) {
            m_focusedAnchorId = anchorIds(primarySurfaceId()).value(0);
        }
        bump();
        return OpResult::success(entry.id);
    });
}

OpResult WorkspaceModel::unminimizeView(QString viewId, QString anchorId, int index, bool activate)
{
    return transact([&]() {
        const QString entryId = entryOfMinimizedView(viewId);
        if (entryId.isEmpty()) {
            return OpResult::failure(QStringLiteral("NOT_MINIMIZED"), QStringLiteral("It isn't put away."));
        }
        if (!m_anchors.contains(anchorId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }
        WellEntry *entry = mutableEntry(entryId);
        entry->minimized.removeIf([&](const MinimizedView &minimized) { return minimized.viewId == viewId; });
        dropEntryIfUnused(entryId);
        insertViewIntoAnchor(viewId, anchorId, index, activate);
        bump();
        return OpResult::success(anchorId);
    });
}

OpResult WorkspaceModel::setViewPinned(QString viewId, bool pinned, QString wellId)
{
    return transact([&]() {
        const ViewRecord *view = this->view(viewId);
        if (!view) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }
        if (view->resource.isEmpty()) {
            return OpResult::failure(QStringLiteral("NOT_PINNABLE"), QStringLiteral("There's nothing to pin here."));
        }
        if (wellId.isEmpty()) {
            const QString minimizedIn = entryOfMinimizedView(viewId);
            wellId = minimizedIn.isEmpty() ? kMainWellId : wellOfEntry(minimizedIn);
        }
        if (!mutableWell(wellId)) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well: ") + wellId);
        }
        QString entryId = wellEntryForResource(wellId, view->resource);
        if (entryId.isEmpty()) {
            if (!pinned) {
                return OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It isn't pinned."));
            }
            WellEntry entry;
            entry.id = newId(QStringLiteral("we"));
            entry.resource = view->resource;
            entry.typeId = view->typeId;
            entry.title = view->title;
            entryId = entry.id;
            mutableWell(wellId)->entries.append(entry);
        }
        WellEntry *entry = mutableEntry(entryId);
        if (entry->pinned == pinned) {
            return OpResult::failure(QStringLiteral("NO_CHANGE"), pinned ? QStringLiteral("It's already pinned.") : QStringLiteral("It isn't pinned."));
        }
        entry->pinned = pinned;
        if (pinned) {
            entry->typeId = view->typeId;
            entry->title = view->title.isEmpty() ? entry->title : view->title;
        }
        dropEntryIfUnused(entryId);
        bump();
        return OpResult::success(entryId);
    });
}

OpResult WorkspaceModel::setWellEntryPinned(QString entryId, bool pinned)
{
    return transact([&]() {
        WellEntry *entry = mutableEntry(entryId);
        if (!entry) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well entry: ") + entryId);
        }
        if (pinned && entry->resource.isEmpty()) {
            return OpResult::failure(QStringLiteral("NOT_PINNABLE"), QStringLiteral("There's nothing to pin here."));
        }
        if (entry->pinned == pinned) {
            return OpResult::failure(QStringLiteral("NO_CHANGE"), pinned ? QStringLiteral("It's already pinned.") : QStringLiteral("It isn't pinned."));
        }
        entry->pinned = pinned;
        dropEntryIfUnused(entryId);
        bump();
        return OpResult::success(entryId);
    });
}

OpResult WorkspaceModel::moveWellEntry(QString entryId, int index)
{
    return transact([&]() {
        WellRecord *record = mutableWell(wellOfEntry(entryId));
        if (!record) {
            return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well entry: ") + entryId);
        }
        qsizetype from = -1;
        for (qsizetype i = 0; i < record->entries.size(); ++i) {
            if (record->entries.at(i).id == entryId) {
                from = i;
            }
        }
        const qsizetype to = std::clamp<qsizetype>(index < 0 ? record->entries.size() - 1 : index, 0, record->entries.size() - 1);
        if (from == to) {
            return OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."));
        }
        record->entries.move(from, to);
        bump();
        return OpResult::success(entryId);
    });
}

OpResult WorkspaceModel::setWellPosition(QString wellId, const QPointF &position)
{
    WellRecord *record = mutableWell(wellId);
    if (!record) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("No such Well: ") + wellId);
    }
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) {
        return OpResult::failure(QStringLiteral("INVALID_POSITION"), QStringLiteral("That isn't a place in the window."));
    }
    const QPointF clamped(std::clamp(position.x(), 0.0, 1.0), std::clamp(position.y(), 0.0, 1.0));
    if (record->position == clamped) {
        return OpResult::success();
    }
    record->position = clamped;
    bump();
    return OpResult::success();
}

QStringList WorkspaceModel::validate() const
{
    QStringList problems;
    QSet<QString> anchorsSeen;
    QSet<QString> nodesSeen;
    int primaryCount = 0;

    for (const QString &surfaceId : m_surfaceOrder) {
        if (!m_surfaces.contains(surfaceId)) {
            problems.append(QStringLiteral("surface order lists unknown %1").arg(surfaceId));
        }
    }

    if (m_surfaceOrder.size() != m_surfaces.size()) {
        problems.append(QStringLiteral("surface order and surfaces disagree"));
    }

    for (auto surfaceIt = m_surfaces.constBegin(); surfaceIt != m_surfaces.constEnd(); ++surfaceIt) {
        const SurfaceRecord &record = surfaceIt.value();

        if (record.primary) {
            ++primaryCount;
        }

        if (record.rootNodeId.isEmpty()) {
            problems.append(QStringLiteral("surface %1 is empty").arg(record.id));
            continue;
        }

        if (!record.maximizedAnchorId.isEmpty() && surfaceOfAnchor(record.maximizedAnchorId) != record.id) {
            problems.append(QStringLiteral("surface %1 maximizes a foreign anchor").arg(record.id));
        }

        // Walk the tree.
        QList<QString> stack{record.rootNodeId};

        if (!m_nodes.value(record.rootNodeId).parentId.isEmpty()) {
            problems.append(QStringLiteral("root %1 has a parent").arg(record.rootNodeId));
        }

        while (!stack.isEmpty()) {
            const QString nodeId = stack.takeLast();

            if (!m_nodes.contains(nodeId)) {
                problems.append(QStringLiteral("missing node %1").arg(nodeId));
                continue;
            }

            if (nodesSeen.contains(nodeId)) {
                problems.append(QStringLiteral("node %1 appears twice").arg(nodeId));
                continue;
            }

            nodesSeen.insert(nodeId);
            const SplitNode &current = m_nodes[nodeId];

            if (current.leaf) {
                if (!m_anchors.contains(current.anchorId)) {
                    problems.append(QStringLiteral("leaf %1 points at missing anchor").arg(nodeId));
                } else if (anchorsSeen.contains(current.anchorId)) {
                    problems.append(QStringLiteral("anchor %1 is placed twice").arg(current.anchorId));
                }
                anchorsSeen.insert(current.anchorId);
                continue;
            }

            if (current.children.size() < 2) {
                problems.append(QStringLiteral("split %1 has fewer than two children").arg(nodeId));
            }

            if (current.sizes.size() != current.children.size()) {
                problems.append(QStringLiteral("split %1 sizes do not match children").arg(nodeId));
            } else {
                double total = 0;
                for (double size : current.sizes) {
                    if (!(size > 0)) {
                        problems.append(QStringLiteral("split %1 has a non-positive size").arg(nodeId));
                    }
                    total += size;
                }
                if (std::abs(total - 1.0) > 1e-6) {
                    problems.append(QStringLiteral("split %1 sizes sum to %2").arg(nodeId).arg(total));
                }
            }

            for (const QString &child : current.children) {
                const SplitNode childNode = m_nodes.value(child);
                if (childNode.parentId != nodeId) {
                    problems.append(QStringLiteral("node %1 has the wrong parent").arg(child));
                }
                if (!childNode.leaf && m_nodes.contains(child) && childNode.orientation == current.orientation) {
                    problems.append(QStringLiteral("split %1 nests a split with the same orientation").arg(nodeId));
                }
                stack.append(child);
            }
        }
    }

    if (primaryCount != 1) {
        problems.append(QStringLiteral("%1 primary surfaces").arg(primaryCount));
    }

    if (nodesSeen.size() != m_nodes.size()) {
        problems.append(QStringLiteral("%1 orphaned nodes").arg(m_nodes.size() - nodesSeen.size()));
    }

    if (anchorsSeen.size() != m_anchors.size()) {
        problems.append(QStringLiteral("%1 anchors are not placed").arg(m_anchors.size() - anchorsSeen.size()));
    }

    QSet<QString> viewsSeen;

    for (auto it = m_anchors.constBegin(); it != m_anchors.constEnd(); ++it) {
        const AnchorRecord &record = it.value();

        for (const QString &viewId : record.views) {
            if (!m_views.contains(viewId)) {
                problems.append(QStringLiteral("anchor %1 lists missing view %2").arg(record.id, viewId));
            }
            if (viewsSeen.contains(viewId)) {
                problems.append(QStringLiteral("view %1 is in two places").arg(viewId));
            }
            viewsSeen.insert(viewId);
            if (m_anchorOfView.value(viewId) != record.id) {
                problems.append(QStringLiteral("view %1 index is stale").arg(viewId));
            }
        }

        if (record.views.isEmpty() ? !record.activeViewId.isEmpty() : !record.views.contains(record.activeViewId)) {
            problems.append(QStringLiteral("anchor %1 has a bad active view").arg(record.id));
        }
    }

    // Gravity Wells: each minimized view is parked once, never also in a
    // panel; entries and Wells have unique ids.
    QSet<QString> wellIds;
    QSet<QString> entryIds;
    bool mainWell = false;
    for (const WellRecord &well : m_wells) {
        if (well.id.isEmpty() || wellIds.contains(well.id)) {
            problems.append(QStringLiteral("Well %1 has no id or appears twice").arg(well.id));
        }
        wellIds.insert(well.id);
        mainWell = mainWell || well.id == kMainWellId;
        if (!well.surfaceId.isEmpty() && !m_surfaces.contains(well.surfaceId)) {
            problems.append(QStringLiteral("Well %1 is in a missing window").arg(well.id));
        }
        QSet<QString> resources;
        for (const WellEntry &entry : well.entries) {
            if (entry.id.isEmpty() || entryIds.contains(entry.id)) {
                problems.append(QStringLiteral("Well entry %1 has no id or appears twice").arg(entry.id));
            }
            entryIds.insert(entry.id);
            if (!entry.resource.isEmpty()) {
                if (resources.contains(entry.resource.key())) {
                    problems.append(QStringLiteral("Well %1 shows %2 twice").arg(well.id, entry.resource.key()));
                }
                resources.insert(entry.resource.key());
            }
            if (!entry.pinned && entry.minimized.isEmpty()) {
                problems.append(QStringLiteral("Well entry %1 holds nothing").arg(entry.id));
            }
            if (entry.resource.isEmpty() && entry.pinned) {
                problems.append(QStringLiteral("Well entry %1 pins nothing").arg(entry.id));
            }
            for (const MinimizedView &minimized : entry.minimized) {
                if (!m_views.contains(minimized.viewId)) {
                    problems.append(QStringLiteral("Well entry %1 lists missing view %2").arg(entry.id, minimized.viewId));
                }
                if (viewsSeen.contains(minimized.viewId)) {
                    problems.append(QStringLiteral("view %1 is in two places").arg(minimized.viewId));
                }
                viewsSeen.insert(minimized.viewId);
            }
        }
    }
    if (!mainWell) {
        problems.append(QStringLiteral("the main Well is missing"));
    }

    if (viewsSeen.size() != m_views.size()) {
        problems.append(QStringLiteral("%1 views are not in any anchor").arg(m_views.size() - viewsSeen.size()));
    }

    if (!m_targetAnchorId.isEmpty() && !m_anchors.contains(m_targetAnchorId)) {
        problems.append(QStringLiteral("target anchor is missing"));
    }

    return problems;
}

WorkspaceModel::Data WorkspaceModel::data() const
{
    Data result;
    result.surfaces = m_surfaces;
    result.surfaceOrder = m_surfaceOrder;
    result.nodes = m_nodes;
    result.anchors = m_anchors;
    result.views = m_views;
    result.focusedAnchorId = m_focusedAnchorId;
    result.targetAnchorId = m_targetAnchorId;
    result.wells = m_wells;
    return result;
}

void WorkspaceModel::repairPointers()
{
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        it->parentId.clear();
    }

    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (!it->leaf) {
            for (const QString &child : it->children) {
                if (m_nodes.contains(child)) {
                    m_nodes[child].parentId = it.key();
                }
            }
        }
    }

    m_anchorOfView.clear();

    for (auto it = m_anchors.constBegin(); it != m_anchors.constEnd(); ++it) {
        for (const QString &viewId : it->views) {
            m_anchorOfView.insert(viewId, it.key());
        }
    }
}

QStringList WorkspaceModel::setData(const Data &data)
{
    WorkspaceModel candidate(m_generateId);
    candidate.m_surfaces = data.surfaces;
    candidate.m_surfaceOrder = data.surfaceOrder;
    candidate.m_nodes = data.nodes;
    candidate.m_anchors = data.anchors;
    candidate.m_views = data.views;
    candidate.m_focusedAnchorId = data.focusedAnchorId;
    candidate.m_targetAnchorId = data.targetAnchorId;
    candidate.m_wells = data.wells;
    candidate.ensureMainWell();
    candidate.repairPointers();

    const QStringList problems = candidate.validate();

    if (!problems.isEmpty()) {
        return problems;
    }

    m_surfaces = candidate.m_surfaces;
    m_surfaceOrder = candidate.m_surfaceOrder;
    m_nodes = candidate.m_nodes;
    m_anchors = candidate.m_anchors;
    m_views = candidate.m_views;
    m_anchorOfView = candidate.m_anchorOfView;
    m_focusedAnchorId = m_anchors.contains(candidate.m_focusedAnchorId)
        ? candidate.m_focusedAnchorId
        : anchorIds(primarySurfaceId()).value(0);
    m_targetAnchorId = candidate.m_targetAnchorId;
    m_wells = candidate.m_wells;
    bump();
    return {};
}
} // namespace Workspace
