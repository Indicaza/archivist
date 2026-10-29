#include "Core/Domains/Workspace/workspace_codec.h"

#include <QJsonArray>
#include <QPointF>
#include <QSet>

#include <algorithm>

namespace Workspace::Codec
{
namespace
{
QJsonObject encodeRect(const QRectF &rect)
{
    if (!rect.isValid()) {
        return {};
    }

    return {
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    };
}

QRectF decodeRect(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QRectF rect(
        object.value(QStringLiteral("x")).toDouble(),
        object.value(QStringLiteral("y")).toDouble(),
        object.value(QStringLiteral("width")).toDouble(),
        object.value(QStringLiteral("height")).toDouble()
    );
    return rect.isValid() ? rect : QRectF();
}

QJsonArray toArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &item : value.toArray()) {
        if (item.isString() && !item.toString().isEmpty()) {
            result.append(item.toString());
        }
    }
    return result;
}

// What an anchor carries in a layout. `withHistory` adds the types and
// resources it holds now, for captured layouts.
QJsonObject encodeAnchor(const WorkspaceModel &model, const AnchorRecord &anchor, bool withHistory)
{
    QJsonObject object{
        {QStringLiteral("id"), anchor.id},
        {QStringLiteral("pinned"), anchor.pinned},
    };

    QStringList roles = anchor.roles;
    QStringList affinity;

    if (withHistory) {
        for (const QString &viewId : anchor.views) {
            const ViewRecord *view = model.view(viewId);
            if (!view) {
                continue;
            }
            if (!roles.contains(view->typeId)) {
                roles.append(view->typeId);
            }
            if (!view->resource.isEmpty() && !affinity.contains(view->resource.key())) {
                affinity.append(view->resource.key());
            }
        }
    }

    if (!roles.isEmpty()) {
        object.insert(QStringLiteral("roles"), toArray(roles));
    }

    if (!affinity.isEmpty()) {
        object.insert(QStringLiteral("affinity"), toArray(affinity));
    }

    // Anchor identity and configuration. Absent means the default, so older
    // snapshots and layouts read unchanged.
    if (!anchor.name.isEmpty()) object.insert(QStringLiteral("name"), anchor.name);
    if (anchor.layoutLocked) object.insert(QStringLiteral("layoutLocked"), true);
    if (anchor.contentLocked) object.insert(QStringLiteral("contentLocked"), true);
    if (anchor.switchable) object.insert(QStringLiteral("switchable"), true);
    if (!anchor.libraryId.isEmpty()) object.insert(QStringLiteral("libraryId"), anchor.libraryId);

    return object;
}

QJsonObject encodeNode(const WorkspaceModel &model, const QString &nodeId, bool withHistory)
{
    const SplitNode *node = model.node(nodeId);

    if (!node) {
        return {};
    }

    if (node->leaf) {
        const AnchorRecord *anchor = model.anchor(node->anchorId);
        return {{QStringLiteral("anchor"), anchor ? encodeAnchor(model, *anchor, withHistory) : QJsonObject()}};
    }

    QJsonArray children;
    QJsonArray sizes;

    for (qsizetype i = 0; i < node->children.size(); ++i) {
        children.append(encodeNode(model, node->children.at(i), withHistory));
        sizes.append(node->sizes.value(i));
    }

    return {
        {QStringLiteral("split"), orientationName(node->orientation)},
        {QStringLiteral("sizes"), sizes},
        {QStringLiteral("children"), children},
    };
}

QJsonArray encodeSurfaces(const WorkspaceModel &model, bool withHistory)
{
    QJsonArray surfaces;

    for (const QString &surfaceId : model.surfaceIds()) {
        const SurfaceRecord *surface = model.surface(surfaceId);
        QJsonObject object{
            {QStringLiteral("id"), surface->id},
            {QStringLiteral("primary"), surface->primary},
            {QStringLiteral("root"), encodeNode(model, surface->rootNodeId, withHistory)},
        };

        if (surface->geometry.isValid()) {
            object.insert(QStringLiteral("geometry"), encodeRect(surface->geometry));
        }
        if (!surface->screenName.isEmpty()) {
            object.insert(QStringLiteral("screen"), surface->screenName);
        }
        if (surface->preferredGeometry.isValid()) {
            object.insert(QStringLiteral("preferredGeometry"), encodeRect(surface->preferredGeometry));
        }
        if (!surface->preferredScreenName.isEmpty()) {
            object.insert(QStringLiteral("preferredScreen"), surface->preferredScreenName);
        }
        if (surface->windowMaximized) {
            object.insert(QStringLiteral("windowMaximized"), true);
        }
        if (surface->inMainWindow) {
            object.insert(QStringLiteral("inMainWindow"), true);
        }
        if (!surface->sidebarSlot.isEmpty()) {
            object.insert(QStringLiteral("sidebarSlot"), surface->sidebarSlot);
        }
        if (!surface->maximizedAnchorId.isEmpty()) {
            object.insert(QStringLiteral("maximizedAnchor"), surface->maximizedAnchorId);
        }

        surfaces.append(object);
    }

    return surfaces;
}

// Layout decoding into raw model data. Anchors come back empty; the caller
// places views.
struct DecodedLayout
{
    WorkspaceModel::Data data;
    QHash<QString, QStringList> affinity; // anchor id -> resource keys
    QStringList anchorOrder;
    QString error;
};

class LayoutReader
{
public:
    DecodedLayout read(const QJsonArray &surfaces)
    {
        bool sawPrimary = false;

        for (const QJsonValue &value : surfaces) {
            const QJsonObject object = value.toObject();
            SurfaceRecord surface;
            surface.id = object.value(QStringLiteral("id")).toString();

            if (surface.id.isEmpty() || m_result.data.surfaces.contains(surface.id)) {
                surface.id = QStringLiteral("w:") + QString::number(++m_counter);
            }

            surface.primary = object.value(QStringLiteral("primary")).toBool() && !sawPrimary;
            sawPrimary = sawPrimary || surface.primary;
            surface.geometry = decodeRect(object.value(QStringLiteral("geometry")));
            surface.screenName = object.value(QStringLiteral("screen")).toString();
            surface.preferredGeometry = decodeRect(object.value(QStringLiteral("preferredGeometry")));
            surface.preferredScreenName = object.value(QStringLiteral("preferredScreen")).toString();
            surface.windowMaximized = object.value(QStringLiteral("windowMaximized")).toBool();
            surface.inMainWindow = !surface.primary && object.value(QStringLiteral("inMainWindow")).toBool();
            surface.sidebarSlot = surface.primary ? QString() : object.value(QStringLiteral("sidebarSlot")).toString();
            if (!surface.sidebarSlot.isEmpty()) {
                surface.inMainWindow = false;
            }
            surface.maximizedAnchorId = object.value(QStringLiteral("maximizedAnchor")).toString();
            m_currentSurface = surface.id;
            surface.rootNodeId = readNode(object.value(QStringLiteral("root")).toObject(), QString());

            if (!m_result.error.isEmpty()) {
                return m_result;
            }

            if (surface.rootNodeId.isEmpty()) {
                continue; // an empty window is not worth restoring
            }

            if (!m_result.data.anchors.contains(surface.maximizedAnchorId)) {
                surface.maximizedAnchorId.clear();
            }

            // One surface per sidebar slot: a second one floats instead.
            if (!surface.sidebarSlot.isEmpty()) {
                for (const SurfaceRecord &other : std::as_const(m_result.data.surfaces)) {
                    if (other.sidebarSlot == surface.sidebarSlot) {
                        surface.sidebarSlot.clear();
                        surface.inMainWindow = true;
                        break;
                    }
                }
            }

            m_result.data.surfaces.insert(surface.id, surface);
            m_result.data.surfaceOrder.append(surface.id);
        }

        if (m_result.data.surfaces.isEmpty()) {
            m_result.error = QStringLiteral("The layout has no windows.");
            return m_result;
        }

        if (!sawPrimary) {
            SurfaceRecord &first = m_result.data.surfaces[m_result.data.surfaceOrder.first()];
            first.primary = true;
            first.inMainWindow = false;
            first.sidebarSlot.clear();
        }

        // Maximized anchors must live in their own window.
        for (auto it = m_result.data.surfaces.begin(); it != m_result.data.surfaces.end(); ++it) {
            if (!it->maximizedAnchorId.isEmpty() && m_surfaceOfAnchor.value(it->maximizedAnchorId) != it.key()) {
                it->maximizedAnchorId.clear();
            }
        }

        return m_result;
    }

private:
    QString readNode(const QJsonObject &object, const QString &parentId)
    {
        if (++m_depth > 64) {
            m_result.error = QStringLiteral("The layout is nested too deeply.");
            return {};
        }

        struct DepthGuard
        {
            int &depth;
            ~DepthGuard() { --depth; }
        } guard{m_depth};

        SplitNode node;
        node.id = QStringLiteral("n:") + QString::number(++m_counter);
        node.parentId = parentId;

        if (object.contains(QStringLiteral("anchor"))) {
            const QJsonObject anchorObject = object.value(QStringLiteral("anchor")).toObject();
            AnchorRecord anchor;
            anchor.id = anchorObject.value(QStringLiteral("id")).toString();

            if (anchor.id.isEmpty() || m_result.data.anchors.contains(anchor.id)) {
                anchor.id = QStringLiteral("a:") + QString::number(++m_counter);
            }

            anchor.pinned = anchorObject.value(QStringLiteral("pinned")).toBool();
            anchor.roles = toStringList(anchorObject.value(QStringLiteral("roles")));
            anchor.name = anchorObject.value(QStringLiteral("name")).toString().simplified().left(80);
            anchor.layoutLocked = anchorObject.value(QStringLiteral("layoutLocked")).toBool();
            anchor.contentLocked = anchorObject.value(QStringLiteral("contentLocked")).toBool();
            anchor.switchable = anchorObject.value(QStringLiteral("switchable")).toBool();
            anchor.libraryId = anchorObject.value(QStringLiteral("libraryId")).toString();
            m_result.affinity.insert(anchor.id, toStringList(anchorObject.value(QStringLiteral("affinity"))));
            m_result.data.anchors.insert(anchor.id, anchor);
            m_result.anchorOrder.append(anchor.id);
            m_surfaceOfAnchor.insert(anchor.id, m_currentSurface);
            node.leaf = true;
            node.anchorId = anchor.id;
            m_result.data.nodes.insert(node.id, node);
            return node.id;
        }

        Orientation orientation;
        if (!parseOrientation(object.value(QStringLiteral("split")).toString(), &orientation)) {
            return {};
        }

        node.leaf = false;
        node.orientation = orientation;
        m_result.data.nodes.insert(node.id, node);

        QStringList children;
        QList<double> sizes;
        const QJsonArray childArray = object.value(QStringLiteral("children")).toArray();
        const QJsonArray sizeArray = object.value(QStringLiteral("sizes")).toArray();

        for (qsizetype i = 0; i < childArray.size(); ++i) {
            const QString childId = readNode(childArray.at(i).toObject(), node.id);
            if (!m_result.error.isEmpty()) {
                return {};
            }
            if (childId.isEmpty()) {
                continue;
            }
            children.append(childId);
            const double size = sizeArray.at(i).toDouble();
            sizes.append(size > 0 ? size : 1.0);
        }

        if (children.isEmpty()) {
            m_result.data.nodes.remove(node.id);
            return {};
        }

        if (children.size() == 1) {
            // A one-child split collapses into that child.
            m_result.data.nodes.remove(node.id);
            m_result.data.nodes[children.first()].parentId = parentId;
            return children.first();
        }

        double total = 0;
        for (double size : sizes) {
            total += size;
        }
        for (double &size : sizes) {
            size /= total;
        }

        // Same-orientation nesting is flattened.
        QStringList flatChildren;
        QList<double> flatSizes;
        for (qsizetype i = 0; i < children.size(); ++i) {
            const SplitNode child = m_result.data.nodes.value(children.at(i));
            if (!child.leaf && child.orientation == orientation) {
                for (qsizetype j = 0; j < child.children.size(); ++j) {
                    flatChildren.append(child.children.at(j));
                    flatSizes.append(sizes.at(i) * child.sizes.value(j));
                    m_result.data.nodes[child.children.at(j)].parentId = node.id;
                }
                m_result.data.nodes.remove(child.id);
            } else {
                flatChildren.append(children.at(i));
                flatSizes.append(sizes.at(i));
            }
        }

        SplitNode &stored = m_result.data.nodes[node.id];
        stored.children = flatChildren;
        stored.sizes = flatSizes;
        return node.id;
    }

    DecodedLayout m_result;
    QHash<QString, QString> m_surfaceOfAnchor;
    QString m_currentSurface;
    int m_counter = 0;
    int m_depth = 0;
};

// Puts every view somewhere, preferring the anchor it was in, then one that
// asked for its resource, then one that prefers its type (spreading across
// equally good anchors), then the target anchor.
void placeViews(
    WorkspaceModel::Data &data,
    const QStringList &anchorOrder,
    const QHash<QString, QStringList> &affinity,
    const QList<ViewRecord> &views,
    const QHash<QString, QString> &requestedAnchor,
    const QHash<QString, QString> &requestedActive,
    const QString &fallbackAnchor,
    QStringList *warnings
)
{
    QHash<QString, int> placed;

    for (const ViewRecord &view : views) {
        QString destination = requestedAnchor.value(view.id);

        if (!data.anchors.contains(destination)) {
            destination.clear();
            const QString key = view.resource.key();

            for (const QString &anchorId : anchorOrder) {
                if (!view.resource.isEmpty() && affinity.value(anchorId).contains(key)) {
                    destination = anchorId;
                    break;
                }
            }
        }

        if (destination.isEmpty()) {
            int fewest = -1;
            for (const QString &anchorId : anchorOrder) {
                if (data.anchors.value(anchorId).roles.contains(view.typeId)
                    && (fewest < 0 || placed.value(anchorId) < fewest)) {
                    destination = anchorId;
                    fewest = placed.value(anchorId);
                }
            }
        }

        if (destination.isEmpty()) {
            destination = fallbackAnchor;
            if (warnings && requestedAnchor.contains(view.id)) {
                warnings->append(QStringLiteral("View %1 moved to the target anchor.").arg(view.id));
            }
        }

        data.anchors[destination].views.append(view.id);
        placed[destination] += 1;
    }

    for (auto it = data.anchors.begin(); it != data.anchors.end(); ++it) {
        const QString wanted = requestedActive.value(it.key());
        it->activeViewId = it->views.contains(wanted) ? wanted : it->views.value(0);
    }
}

QString firstPrimaryAnchor(const WorkspaceModel::Data &data, const QStringList &anchorOrder)
{
    QString primaryRoot;
    for (const SurfaceRecord &surface : data.surfaces) {
        if (surface.primary) {
            primaryRoot = surface.rootNodeId;
        }
    }

    // Walk down the primary tree to its first leaf.
    QString current = primaryRoot;
    while (data.nodes.contains(current) && !data.nodes.value(current).leaf) {
        current = data.nodes.value(current).children.value(0);
    }

    return data.nodes.contains(current) ? data.nodes.value(current).anchorId : anchorOrder.value(0);
}
} // namespace

QJsonObject encodeResource(const ResourceRef &resource)
{
    QJsonObject object;
    if (!resource.kind.isEmpty()) object.insert(QStringLiteral("kind"), resource.kind);
    if (!resource.id.isEmpty()) object.insert(QStringLiteral("id"), resource.id);
    if (!resource.collectionId.isEmpty()) object.insert(QStringLiteral("collectionId"), resource.collectionId);
    if (!resource.libraryId.isEmpty()) object.insert(QStringLiteral("libraryId"), resource.libraryId);
    if (!resource.details.isEmpty()) object.insert(QStringLiteral("details"), QJsonObject::fromVariantMap(resource.details));
    return object;
}

ResourceRef decodeResource(const QJsonObject &object)
{
    ResourceRef resource;
    resource.kind = object.value(QStringLiteral("kind")).toString();
    resource.id = object.value(QStringLiteral("id")).toString();
    resource.collectionId = object.value(QStringLiteral("collectionId")).toString();
    resource.libraryId = object.value(QStringLiteral("libraryId")).toString();
    resource.details = object.value(QStringLiteral("details")).toObject().toVariantMap();
    return resource;
}

QJsonObject encodeView(const ViewRecord &view)
{
    QJsonObject object{
        {QStringLiteral("id"), view.id},
        {QStringLiteral("type"), view.typeId},
        {QStringLiteral("stateVersion"), view.stateVersion},
    };
    const QJsonObject resource = encodeResource(view.resource);
    if (!resource.isEmpty()) object.insert(QStringLiteral("resource"), resource);
    if (!view.title.isEmpty()) object.insert(QStringLiteral("title"), view.title);
    if (!view.state.isEmpty()) object.insert(QStringLiteral("state"), QJsonObject::fromVariantMap(view.state));
    return object;
}

ViewRecord decodeView(const QJsonObject &object)
{
    ViewRecord view;
    view.id = object.value(QStringLiteral("id")).toString();
    view.typeId = object.value(QStringLiteral("type")).toString();
    view.stateVersion = std::max(1, object.value(QStringLiteral("stateVersion")).toInt(1));
    view.resource = decodeResource(object.value(QStringLiteral("resource")).toObject());
    view.title = object.value(QStringLiteral("title")).toString();
    view.state = object.value(QStringLiteral("state")).toObject().toVariantMap();
    return view;
}

namespace
{
QJsonObject encodePlace(const Place &place)
{
    QJsonObject object{
        {QStringLiteral("surface"), place.surfaceId},
        {QStringLiteral("anchor"), place.anchorId},
        {QStringLiteral("index"), place.index},
    };
    QJsonObject beside{{QStringLiteral("edge"), edgeName(place.beside.edge)}};
    if (!place.beside.surfaceId.isEmpty()) beside.insert(QStringLiteral("surface"), place.beside.surfaceId);
    if (!place.beside.anchorId.isEmpty()) beside.insert(QStringLiteral("anchor"), place.beside.anchorId);
    object.insert(QStringLiteral("beside"), beside);
    if (place.share > 0) {
        object.insert(QStringLiteral("share"), place.share);
        object.insert(QStringLiteral("orientation"), orientationName(place.splitOrientation));
    }
    if (place.aloneInFloatingWindow) {
        QJsonObject window{{QStringLiteral("geometry"), encodeRect(place.windowGeometry)}};
        if (place.inMainWindow) window.insert(QStringLiteral("inMainWindow"), true);
        if (!place.sidebarSlot.isEmpty()) window.insert(QStringLiteral("sidebarSlot"), place.sidebarSlot);
        if (!place.screenName.isEmpty()) window.insert(QStringLiteral("screen"), place.screenName);
        object.insert(QStringLiteral("window"), window);
    }
    return object;
}

Place decodePlace(const QJsonObject &object, const QString &viewId)
{
    Place place;
    place.source = DragSource::view(viewId);
    place.surfaceId = object.value(QStringLiteral("surface")).toString();
    place.anchorId = object.value(QStringLiteral("anchor")).toString();
    place.index = object.value(QStringLiteral("index")).toInt(-1);
    const QJsonObject beside = object.value(QStringLiteral("beside")).toObject();
    place.beside.surfaceId = beside.value(QStringLiteral("surface")).toString();
    place.beside.anchorId = beside.value(QStringLiteral("anchor")).toString();
    Edge edge = Edge::Center;
    if (parseEdge(beside.value(QStringLiteral("edge")).toString(), &edge)) {
        place.beside.edge = edge;
    }
    place.share = std::clamp(object.value(QStringLiteral("share")).toDouble(0), 0.0, 1.0);
    Orientation orientation = Orientation::Horizontal;
    if (parseOrientation(object.value(QStringLiteral("orientation")).toString(), &orientation)) {
        place.splitOrientation = orientation;
    }
    if (object.contains(QStringLiteral("window"))) {
        const QJsonObject window = object.value(QStringLiteral("window")).toObject();
        place.aloneInFloatingWindow = true;
        place.windowGeometry = decodeRect(window.value(QStringLiteral("geometry")));
        place.inMainWindow = window.value(QStringLiteral("inMainWindow")).toBool();
        place.sidebarSlot = window.value(QStringLiteral("sidebarSlot")).toString();
        place.screenName = window.value(QStringLiteral("screen")).toString();
    }
    return place;
}

// Wells and the views put away in them. The minimized views themselves are
// written with the session's views (in no panel), so an older Archivist
// that doesn't know Wells still opens them as tabs instead of losing them.
QJsonArray encodeWells(const WorkspaceModel &model)
{
    QJsonArray wells;
    for (const WellRecord &well : model.wells()) {
        QJsonArray entries;
        for (const WellEntry &entry : well.entries) {
            QJsonArray minimized;
            for (const MinimizedView &view : entry.minimized) {
                minimized.append(QJsonObject{
                    {QStringLiteral("view"), view.viewId},
                    {QStringLiteral("place"), encodePlace(view.place)},
                });
            }
            QJsonObject object{{QStringLiteral("id"), entry.id}};
            const QJsonObject resource = encodeResource(entry.resource);
            if (!resource.isEmpty()) object.insert(QStringLiteral("resource"), resource);
            if (!entry.typeId.isEmpty()) object.insert(QStringLiteral("type"), entry.typeId);
            if (!entry.title.isEmpty()) object.insert(QStringLiteral("title"), entry.title);
            if (entry.pinned) object.insert(QStringLiteral("pinned"), true);
            if (!minimized.isEmpty()) object.insert(QStringLiteral("minimized"), minimized);
            entries.append(object);
        }
        QJsonObject object{
            {QStringLiteral("id"), well.id},
            {QStringLiteral("x"), well.position.x()},
            {QStringLiteral("y"), well.position.y()},
            {QStringLiteral("orientation"), orientationName(well.orientation)},
            {QStringLiteral("form"), wellFormName(well.form)},
            {QStringLiteral("entries"), entries},
        };
        if (!well.surfaceId.isEmpty()) object.insert(QStringLiteral("surface"), well.surfaceId);
        wells.append(object);
    }
    return wells;
}

// Reads Wells, keeping only what still makes sense: minimized views that
// exist in `views` (each once), pinned entries with a resource, Wells in a
// window that exists (else the main window). Fills `minimizedIds`.
QList<WellRecord> decodeWells(
    const QJsonArray &array,
    const QHash<QString, ViewRecord> &views,
    const QHash<QString, SurfaceRecord> &surfaces,
    QSet<QString> *minimizedIds,
    QStringList *warnings
)
{
    QList<WellRecord> wells;
    QSet<QString> wellIds;
    QSet<QString> entryIds;
    for (const QJsonValue &wellValue : array) {
        const QJsonObject object = wellValue.toObject();
        WellRecord well;
        well.id = object.value(QStringLiteral("id")).toString();
        if (well.id.isEmpty() || wellIds.contains(well.id)) {
            warnings->append(QStringLiteral("Skipped an unreadable Gravity Well."));
            continue;
        }
        wellIds.insert(well.id);
        well.surfaceId = object.value(QStringLiteral("surface")).toString();
        if (!well.surfaceId.isEmpty() && !surfaces.contains(well.surfaceId)) {
            well.surfaceId.clear();
        }
        well.position = QPointF(
            std::clamp(object.value(QStringLiteral("x")).toDouble(0.5), 0.0, 1.0),
            std::clamp(object.value(QStringLiteral("y")).toDouble(1.0), 0.0, 1.0)
        );
        Orientation orientation = Orientation::Horizontal;
        if (parseOrientation(object.value(QStringLiteral("orientation")).toString(), &orientation)) {
            well.orientation = orientation;
        }
        WellForm form = WellForm::Linear;
        if (parseWellForm(object.value(QStringLiteral("form")).toString(), &form)) {
            well.form = form;
        }
        QSet<QString> resources;
        for (const QJsonValue &entryValue : object.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject entryObject = entryValue.toObject();
            WellEntry entry;
            entry.id = entryObject.value(QStringLiteral("id")).toString();
            entry.resource = decodeResource(entryObject.value(QStringLiteral("resource")).toObject());
            entry.typeId = entryObject.value(QStringLiteral("type")).toString();
            entry.title = entryObject.value(QStringLiteral("title")).toString();
            entry.pinned = entryObject.value(QStringLiteral("pinned")).toBool() && !entry.resource.isEmpty();
            for (const QJsonValue &minimizedValue : entryObject.value(QStringLiteral("minimized")).toArray()) {
                const QJsonObject minimizedObject = minimizedValue.toObject();
                const QString viewId = minimizedObject.value(QStringLiteral("view")).toString();
                if (!views.contains(viewId) || minimizedIds->contains(viewId)) {
                    continue;
                }
                minimizedIds->insert(viewId);
                entry.minimized.append(MinimizedView{viewId, decodePlace(minimizedObject.value(QStringLiteral("place")).toObject(), viewId)});
            }
            const bool duplicate = !entry.resource.isEmpty() && resources.contains(entry.resource.key());
            if (entry.id.isEmpty() || entryIds.contains(entry.id) || duplicate || (!entry.pinned && entry.minimized.isEmpty())) {
                for (const MinimizedView &view : std::as_const(entry.minimized)) {
                    minimizedIds->remove(view.viewId); // it opens as a tab instead
                }
                continue;
            }
            entryIds.insert(entry.id);
            if (!entry.resource.isEmpty()) {
                resources.insert(entry.resource.key());
            }
            well.entries.append(entry);
        }
        wells.append(well);
    }
    return wells;
}
} // namespace

QJsonObject encodeSnapshot(const WorkspaceModel &model)
{
    QJsonArray views;
    QJsonArray anchors;

    for (const QString &anchorId : model.anchorIds()) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        anchors.append(QJsonObject{
            {QStringLiteral("id"), anchor->id},
            {QStringLiteral("views"), toArray(anchor->views)},
            {QStringLiteral("active"), anchor->activeViewId},
        });

        for (const QString &viewId : anchor->views) {
            views.append(encodeView(*model.view(viewId)));
        }
    }
    for (const QString &viewId : model.minimizedViewIds()) {
        views.append(encodeView(*model.view(viewId)));
    }

    QJsonObject session{
        {QStringLiteral("views"), views},
        {QStringLiteral("anchors"), anchors},
        {QStringLiteral("wells"), encodeWells(model)},
    };
    if (!model.focusedAnchorId().isEmpty()) session.insert(QStringLiteral("focusedAnchor"), model.focusedAnchorId());
    if (!model.explicitTargetAnchorId().isEmpty()) session.insert(QStringLiteral("targetAnchor"), model.explicitTargetAnchorId());

    return {
        {QStringLiteral("format"), SnapshotFormat},
        {QStringLiteral("version"), kSnapshotVersion},
        {QStringLiteral("layout"), QJsonObject{{QStringLiteral("surfaces"), encodeSurfaces(model, false)}}},
        {QStringLiteral("session"), session},
    };
}

LoadReport decodeSnapshot(const QJsonObject &snapshot, const ViewTypeRegistry &registry, WorkspaceModel &model)
{
    LoadReport report;

    if (snapshot.value(QStringLiteral("format")).toString() != SnapshotFormat) {
        report.error = QStringLiteral("Not an Archivist workspace snapshot.");
        return report;
    }

    const int version = snapshot.value(QStringLiteral("version")).toInt();

    if (version > kSnapshotVersion) {
        report.error = QStringLiteral("This workspace was saved by a newer Archivist (format %1).").arg(version);
        return report;
    }

    if (version < 1) {
        report.error = QStringLiteral("Unknown workspace format version.");
        return report;
    }

    DecodedLayout layout = LayoutReader().read(
        snapshot.value(QStringLiteral("layout")).toObject().value(QStringLiteral("surfaces")).toArray()
    );

    if (!layout.error.isEmpty()) {
        report.error = layout.error;
        return report;
    }

    const QJsonObject session = snapshot.value(QStringLiteral("session")).toObject();
    QList<ViewRecord> views;
    QSet<QString> viewIds;

    for (const QJsonValue &value : session.value(QStringLiteral("views")).toArray()) {
        ViewRecord view = decodeView(value.toObject());

        if (view.id.isEmpty() || view.typeId.isEmpty() || viewIds.contains(view.id)) {
            report.warnings.append(QStringLiteral("Skipped an unreadable or duplicate view."));
            continue;
        }

        if (!registry.contains(view.typeId)) {
            report.warnings.append(QStringLiteral("Kept view %1 of unknown type %2.").arg(view.id, view.typeId));
        }

        bool migrated = false;
        view = registry.migrate(view, &migrated);
        report.migratedViews += migrated ? 1 : 0;
        viewIds.insert(view.id);
        views.append(view);
        layout.data.views.insert(view.id, view);
    }

    // Views put away in a Well stay there (not placed in a panel).
    QSet<QString> minimizedIds;
    layout.data.wells = decodeWells(
        session.value(QStringLiteral("wells")).toArray(), layout.data.views, layout.data.surfaces, &minimizedIds, &report.warnings
    );

    QHash<QString, QString> requestedAnchor;
    QHash<QString, QString> requestedActive;
    // Keep each anchor's saved tab order by visiting anchors in session order.
    QList<ViewRecord> ordered;
    QSet<QString> orderedIds;

    for (const QJsonValue &value : session.value(QStringLiteral("anchors")).toArray()) {
        const QJsonObject anchor = value.toObject();
        const QString anchorId = anchor.value(QStringLiteral("id")).toString();
        requestedActive.insert(anchorId, anchor.value(QStringLiteral("active")).toString());

        for (const QString &viewId : toStringList(anchor.value(QStringLiteral("views")))) {
            if (viewIds.contains(viewId) && !orderedIds.contains(viewId) && !minimizedIds.contains(viewId)) {
                requestedAnchor.insert(viewId, anchorId);
                ordered.append(layout.data.views.value(viewId));
                orderedIds.insert(viewId);
            }
        }
    }

    for (const ViewRecord &view : views) {
        if (!orderedIds.contains(view.id) && !minimizedIds.contains(view.id)) {
            requestedAnchor.insert(view.id, QString()); // was nowhere: target anchor
            ordered.append(view);
        }
    }

    QString fallback = session.value(QStringLiteral("targetAnchor")).toString();
    if (!layout.data.anchors.contains(fallback)) {
        fallback = firstPrimaryAnchor(layout.data, layout.anchorOrder);
    }

    placeViews(layout.data, layout.anchorOrder, {}, ordered, requestedAnchor, requestedActive, fallback, &report.warnings);

    layout.data.focusedAnchorId = session.value(QStringLiteral("focusedAnchor")).toString();
    const QString target = session.value(QStringLiteral("targetAnchor")).toString();
    layout.data.targetAnchorId = layout.data.anchors.contains(target) ? target : QString();

    // Incidental anchors that came back empty are dropped; pinned ones stay.
    WorkspaceModel candidate = model;
    QStringList problems = candidate.setData(layout.data);

    if (!problems.isEmpty()) {
        report.error = QStringLiteral("The saved workspace is inconsistent: ") + problems.join(QStringLiteral("; "));
        return report;
    }

    for (const QString &anchorId : candidate.anchorIds()) {
        const AnchorRecord *anchor = candidate.anchor(anchorId);
        const bool intentional = anchor && (anchor->pinned || anchor->layoutLocked || anchor->contentLocked
            || anchor->switchable || !anchor->name.isEmpty() || !anchor->libraryId.isEmpty());
        if (anchor && anchor->views.isEmpty() && !intentional && candidate.anchorIds().size() > 1) {
            candidate.closeAnchor(anchorId);
        }
    }

    model = candidate;
    report.ok = true;
    return report;
}

QJsonObject captureLayout(const WorkspaceModel &model, const QString &name)
{
    QJsonObject layout{
        {QStringLiteral("format"), LayoutFormat},
        {QStringLiteral("version"), kLayoutVersion},
        {QStringLiteral("name"), name},
        {QStringLiteral("surfaces"), encodeSurfaces(model, true)},
    };

    if (!model.explicitTargetAnchorId().isEmpty()) {
        layout.insert(QStringLiteral("targetAnchor"), model.explicitTargetAnchorId());
    }

    return layout;
}

bool isLayout(const QJsonObject &object)
{
    return object.value(QStringLiteral("format")).toString() == LayoutFormat;
}

LoadReport applyLayout(const QJsonObject &layoutObject, WorkspaceModel &model)
{
    LoadReport report;

    if (!isLayout(layoutObject)) {
        report.error = QStringLiteral("Not an Archivist layout.");
        return report;
    }

    if (layoutObject.value(QStringLiteral("version")).toInt() > kLayoutVersion) {
        report.error = QStringLiteral("This layout was saved by a newer Archivist.");
        return report;
    }

    // A layout rebuilds every panel, which a layout lock forbids.
    const QStringList locked = model.layoutLockedAnchorIds();
    if (!locked.isEmpty()) {
        report.error = QStringLiteral("“%1” has a locked layout, so the layout can't change. Unlock it first.")
            .arg(model.anchorDisplayName(locked.first()));
        return report;
    }

    DecodedLayout layout = LayoutReader().read(layoutObject.value(QStringLiteral("surfaces")).toArray());

    if (!layout.error.isEmpty()) {
        report.error = layout.error;
        return report;
    }

    // The main window stays where the user has it.
    const QString currentPrimary = model.primarySurfaceId();
    for (auto it = layout.data.surfaces.begin(); it != layout.data.surfaces.end(); ++it) {
        if (it->primary && model.surface(currentPrimary)) {
            const SurfaceRecord *current = model.surface(currentPrimary);
            it->geometry = current->geometry;
            it->screenName = current->screenName;
            it->preferredGeometry = current->preferredGeometry;
            it->preferredScreenName = current->preferredScreenName;
            it->windowMaximized = current->windowMaximized;
        }
    }

    // What is docked in the sidebar stays there: a layout arranges the
    // windows, not the sidebar. Those surfaces come along whole.
    const WorkspaceModel::Data current = model.data();
    QSet<QString> keptViews;
    for (const QString &surfaceId : current.surfaceOrder) {
        const SurfaceRecord surface = current.surfaces.value(surfaceId);
        if (surface.sidebarSlot.isEmpty() || layout.data.surfaces.contains(surfaceId)) {
            continue;
        }
        QStringList stack{surface.rootNodeId};
        QList<SplitNode> nodes;
        bool clash = false;
        while (!stack.isEmpty()) {
            const SplitNode node = current.nodes.value(stack.takeLast());
            if (node.id.isEmpty() || layout.data.nodes.contains(node.id)
                || (node.leaf && layout.data.anchors.contains(node.anchorId))) {
                clash = true;
                break;
            }
            nodes.append(node);
            stack.append(node.children);
        }
        if (clash) {
            continue; // its tabs go into the layout like any others
        }
        for (const SplitNode &node : std::as_const(nodes)) {
            layout.data.nodes.insert(node.id, node);
            if (node.leaf) {
                const AnchorRecord anchor = current.anchors.value(node.anchorId);
                layout.data.anchors.insert(anchor.id, anchor);
                for (const QString &viewId : anchor.views) {
                    keptViews.insert(viewId);
                }
            }
        }
        layout.data.surfaces.insert(surfaceId, surface);
        layout.data.surfaceOrder.append(surfaceId);
    }

    QList<ViewRecord> views;
    for (const QString &viewId : model.viewIds()) {
        layout.data.views.insert(viewId, *model.view(viewId));
        if (!keptViews.contains(viewId)) {
            views.append(*model.view(viewId));
        }
    }
    // A layout arranges panels: what is put away in a Well stays there.
    for (const QString &viewId : model.minimizedViewIds()) {
        layout.data.views.insert(viewId, *model.view(viewId));
    }
    layout.data.wells = current.wells;
    for (WellRecord &well : layout.data.wells) {
        if (!well.surfaceId.isEmpty() && !layout.data.surfaces.contains(well.surfaceId)) {
            well.surfaceId.clear();
        }
    }

    // Keep each anchor's current active tab active in its new home.
    QSet<QString> activeViews;
    for (const QString &anchorId : model.anchorIds()) {
        activeViews.insert(model.anchor(anchorId)->activeViewId);
    }

    QString fallback = layoutObject.value(QStringLiteral("targetAnchor")).toString();
    if (!layout.data.anchors.contains(fallback)) {
        fallback = firstPrimaryAnchor(layout.data, layout.anchorOrder);
    }

    placeViews(layout.data, layout.anchorOrder, layout.affinity, views, {}, {}, fallback, &report.warnings);

    for (auto it = layout.data.anchors.begin(); it != layout.data.anchors.end(); ++it) {
        for (const QString &viewId : it->views) {
            if (activeViews.contains(viewId)) {
                it->activeViewId = viewId;
                break;
            }
        }
    }

    // Focus follows the content that had it.
    const AnchorRecord *focused = model.anchor(model.focusedAnchorId());
    layout.data.focusedAnchorId = fallback;
    if (focused && !focused->activeViewId.isEmpty()) {
        for (auto it = layout.data.anchors.constBegin(); it != layout.data.anchors.constEnd(); ++it) {
            if (it->views.contains(focused->activeViewId)) {
                layout.data.focusedAnchorId = it.key();
            }
        }
    }

    const QString target = layoutObject.value(QStringLiteral("targetAnchor")).toString();
    layout.data.targetAnchorId = layout.data.anchors.contains(target) ? target : QString();

    const QStringList problems = model.setData(layout.data);

    if (!problems.isEmpty()) {
        report.error = QStringLiteral("The layout could not be applied: ") + problems.join(QStringLiteral("; "));
        return report;
    }

    report.ok = true;
    return report;
}
} // namespace Workspace::Codec
