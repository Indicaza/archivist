#include "Core/Domains/Workspace/workspace_commands.h"

namespace Workspace::Commands
{
namespace
{
const QString kAnchorList = QStringLiteral("anchor.list");
const QString kAnchorDescribe = QStringLiteral("anchor.describe");
const QString kAnchorRename = QStringLiteral("anchor.rename");
const QString kAnchorLayoutLock = QStringLiteral("anchor.setLayoutLock");
const QString kAnchorContentLock = QStringLiteral("anchor.setContentLock");
const QString kAnchorSwitchable = QStringLiteral("anchor.setSwitchable");
const QString kAnchorWorktree = QStringLiteral("anchor.setWorktree");
const QString kAnchorAdd = QStringLiteral("anchor.addResource");
const QString kAnchorRemove = QStringLiteral("anchor.removeResource");
const QString kAnchorSelect = QStringLiteral("anchor.selectResource");
const QString kAnchorReorder = QStringLiteral("anchor.reorderResource");
const QString kViewOpen = QStringLiteral("view.open");
const QString kViewMove = QStringLiteral("view.move");

QString text(const QVariantMap &command, const char *key)
{
    return command.value(QString::fromLatin1(key)).toString();
}

Result fail(const QString &code, const QString &message)
{
    return {OpResult::failure(code, message), {}};
}

Result openInto(WorkspaceModel &model, const QVariantMap &command, const QString &anchorId, Origin origin)
{
    ViewRecord view;
    view.typeId = text(command, "typeId");
    view.resource = resourceFromMap(command.value(QStringLiteral("resource")).toMap());
    view.title = text(command, "title");
    view.state = command.value(QStringLiteral("state")).toMap();

    if (view.typeId.isEmpty()) {
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("A view type is needed."));
    }

    const OpResult result = model.openView(
        view,
        anchorId,
        command.value(QStringLiteral("index"), -1).toInt(),
        command.value(QStringLiteral("activate"), true).toBool(),
        origin
    );
    return {result, result.ok ? QVariantMap{{QStringLiteral("viewId"), result.createdId}} : QVariantMap()};
}
} // namespace

QStringList names()
{
    return {
        kAnchorList,
        kAnchorDescribe,
        kAnchorRename,
        kAnchorLayoutLock,
        kAnchorContentLock,
        kAnchorSwitchable,
        kAnchorWorktree,
        kAnchorAdd,
        kAnchorRemove,
        kAnchorSelect,
        kAnchorReorder,
        kViewOpen,
        kViewMove,
    };
}

bool isQuery(const QString &name)
{
    return name == kAnchorList || name == kAnchorDescribe;
}

ResourceRef resourceFromMap(const QVariantMap &map)
{
    ResourceRef resource;
    resource.kind = map.value(QStringLiteral("kind")).toString();
    resource.id = map.value(QStringLiteral("id")).toString();
    resource.collectionId = map.value(QStringLiteral("collectionId")).toString();
    resource.libraryId = map.value(QStringLiteral("libraryId")).toString();
    resource.details = map.value(QStringLiteral("details")).toMap();
    return resource;
}

QVariantMap resourceToMap(const ResourceRef &resource)
{
    return {
        {QStringLiteral("kind"), resource.kind},
        {QStringLiteral("id"), resource.id},
        {QStringLiteral("collectionId"), resource.collectionId},
        {QStringLiteral("libraryId"), resource.libraryId},
        {QStringLiteral("details"), resource.details},
    };
}

QVariantMap capabilitiesMap(const AnchorCapabilities &capabilities)
{
    return {
        {QStringLiteral("move"), capabilities.canMove},
        {QStringLiteral("close"), capabilities.canClose},
        {QStringLiteral("resize"), capabilities.canResize},
        {QStringLiteral("merge"), capabilities.canMerge},
        {QStringLiteral("float"), capabilities.canFloat},
        {QStringLiteral("dockBeside"), capabilities.canDockBeside},
        {QStringLiteral("addViews"), capabilities.canAddViews},
        {QStringLiteral("removeViews"), capabilities.canRemoveViews},
        {QStringLiteral("replaceContent"), capabilities.canReplaceContent},
        {QStringLiteral("rename"), capabilities.canRename},
        {QStringLiteral("unlock"), capabilities.canUnlock},
        {QStringLiteral("setWorktree"), capabilities.canSetWorktree},
    };
}

QVariantMap describeAnchor(const WorkspaceModel &model, const QString &anchorId, Origin origin)
{
    const AnchorRecord *anchor = model.anchor(anchorId);

    if (!anchor) {
        return {};
    }

    QVariantList views;
    for (const QString &viewId : anchor->views) {
        const ViewRecord *view = model.view(viewId);
        if (!view) {
            continue;
        }
        views.append(QVariantMap{
            {QStringLiteral("viewId"), view->id},
            {QStringLiteral("typeId"), view->typeId},
            {QStringLiteral("title"), view->title},
            {QStringLiteral("resource"), resourceToMap(view->resource)},
            {QStringLiteral("active"), view->id == anchor->activeViewId},
        });
    }

    const AnchorCapabilities capabilities = model.anchorCapabilities(anchorId, origin);
    QVariantMap restrictions;
    for (auto it = capabilities.reasons.cbegin(); it != capabilities.reasons.cend(); ++it) {
        restrictions.insert(it.key(), it.value());
    }

    return {
        {QStringLiteral("anchorId"), anchorId},
        {QStringLiteral("name"), anchor->name},
        {QStringLiteral("displayName"), model.anchorDisplayName(anchorId)},
        {QStringLiteral("automaticName"), model.automaticAnchorName(anchorId)},
        {QStringLiteral("pinned"), anchor->pinned},
        {QStringLiteral("layoutLocked"), anchor->layoutLocked},
        {QStringLiteral("contentLocked"), anchor->contentLocked},
        {QStringLiteral("switchable"), anchor->switchable},
        {QStringLiteral("libraryId"), anchor->libraryId},
        {QStringLiteral("surfaceId"), model.surfaceOfAnchor(anchorId)},
        {QStringLiteral("activeViewId"), anchor->activeViewId},
        {QStringLiteral("focused"), model.focusedAnchorId() == anchorId},
        {QStringLiteral("target"), model.explicitTargetAnchorId() == anchorId},
        {QStringLiteral("views"), views},
        {QStringLiteral("capabilities"), capabilitiesMap(capabilities)},
        {QStringLiteral("restrictions"), restrictions},
    };
}

QVariantMap resultMap(const Result &result)
{
    QVariantMap map = result.data;
    map.insert(QStringLiteral("ok"), result.op.ok);
    if (!result.op.ok) {
        map.insert(QStringLiteral("code"), result.op.code);
        map.insert(QStringLiteral("error"), result.op.message);
    }
    if (!result.op.createdId.isEmpty() && !map.contains(QStringLiteral("createdId"))) {
        map.insert(QStringLiteral("createdId"), result.op.createdId);
    }
    return map;
}

Result execute(WorkspaceModel &model, const QVariantMap &command, Origin origin)
{
    const QString name = text(command, "command");
    const QString anchorId = text(command, "anchorId");
    const QString viewId = text(command, "viewId");

    if (name == kAnchorList) {
        QVariantList anchors;
        for (const QString &id : model.anchorIds()) {
            anchors.append(describeAnchor(model, id, origin));
        }
        return {OpResult::success(), {{QStringLiteral("anchors"), anchors}}};
    }

    if (name == kAnchorDescribe) {
        const QVariantMap description = describeAnchor(model, anchorId, origin);
        if (description.isEmpty()) {
            return fail(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }
        return {OpResult::success(), description};
    }

    if (name == kAnchorRename) {
        return {model.renameAnchor(anchorId, text(command, "name"), origin), {}};
    }
    if (name == kAnchorLayoutLock) {
        return {model.setAnchorLayoutLocked(anchorId, command.value(QStringLiteral("locked"), true).toBool(), origin), {}};
    }
    if (name == kAnchorContentLock) {
        return {model.setAnchorContentLocked(anchorId, command.value(QStringLiteral("locked"), true).toBool(), origin), {}};
    }
    if (name == kAnchorSwitchable) {
        return {model.setAnchorSwitchable(anchorId, command.value(QStringLiteral("switchable"), true).toBool(), origin), {}};
    }
    if (name == kAnchorWorktree) {
        return {model.setAnchorLibrary(anchorId, text(command, "libraryId"), origin), {}};
    }

    if (name == kAnchorAdd) {
        if (!model.anchor(anchorId)) {
            return fail(QStringLiteral("NOT_FOUND"), QStringLiteral("No such anchor: ") + anchorId);
        }
        return openInto(model, command, anchorId, origin);
    }
    if (name == kViewOpen) {
        return openInto(model, command, anchorId, origin);
    }

    if (name == kAnchorRemove) {
        return {model.closeView(viewId, origin), {}};
    }
    if (name == kAnchorSelect) {
        // Showing another of an anchor's own views is navigation, which a
        // content lock allows.
        const QString focused = model.focusedAnchorId();
        const OpResult result = model.activateView(viewId);
        // Only a person moves keyboard focus.
        if (result.ok && origin != Origin::User && !focused.isEmpty()) {
            model.focusAnchor(focused);
        }
        return {result, {}};
    }
    if (name == kAnchorReorder) {
        const QString owner = model.anchorOfView(viewId);
        if (owner.isEmpty()) {
            return fail(QStringLiteral("NOT_FOUND"), QStringLiteral("No such view: ") + viewId);
        }
        return {model.moveView(viewId, owner, command.value(QStringLiteral("index"), -1).toInt(), origin), {}};
    }
    if (name == kViewMove) {
        return {model.moveView(viewId, anchorId, command.value(QStringLiteral("index"), -1).toInt(), origin), {}};
    }

    return fail(QStringLiteral("UNKNOWN_COMMAND"), QStringLiteral("Unknown workspace command: ") + name);
}
} // namespace Workspace::Commands
