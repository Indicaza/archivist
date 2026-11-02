// workspace_* tools: see, show, arrange, compose, restore, save and prefer
// workspace arrangements. They run on the AI thread and reach the workspace
// through Workspace::Gateway (or Context::workspace), which applies them on
// the UI thread under the same locks a person meets, or stages them as a
// suggestion when the agent works in the background. Only this file depends
// on the Workspace domain.
#include "ai_tools_definition.h"
#include "ai_tools_input.h"

#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Workspace/workspace_compose_catalog.h"
#include "Core/Domains/Workspace/workspace_gateway.h"
#include "Core/Support/js_compat.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace AITools::Input;
using AITools::Context;
using AITools::ToolError;

namespace
{
const QStringList &showKinds()
{
    static const QStringList kinds{
        QStringLiteral("file"), QStringLiteral("chat"), QStringLiteral("terminal"), QStringLiteral("diff"), QStringLiteral("open_view"),
    };
    return kinds;
}

const QStringList &arrangeActions()
{
    static const QStringList actions{
        QStringLiteral("focus"), QStringLiteral("resize"), QStringLiteral("move"), QStringLiteral("keep"), QStringLiteral("label"),
        QStringLiteral("add"), QStringLiteral("remove"),
    };
    return actions;
}

const QStringList &restoreScopes()
{
    static const QStringList scopes{QStringLiteral("last_change"), QStringLiteral("redo"), QStringLiteral("before_task"), QStringLiteral("saved_layout")};
    return scopes;
}

const QStringList &layoutOps()
{
    static const QStringList ops{QStringLiteral("list"), QStringLiteral("apply"), QStringLiteral("save"), QStringLiteral("rename"), QStringLiteral("delete")};
    return ops;
}

const QStringList &preferenceOps()
{
    static const QStringList ops{QStringLiteral("set"), QStringLiteral("clear"), QStringLiteral("list")};
    return ops;
}

const QStringList &preferenceKeys()
{
    static const QStringList keys{QStringLiteral("chat_location"), QStringLiteral("main_editor"), QStringLiteral("rearranging"), QStringLiteral("area_purpose")};
    return keys;
}

const QStringList &preferenceDurations()
{
    static const QStringList durations{QStringLiteral("this_session"), QStringLiteral("always")};
    return durations;
}

// Fixed field lists, shared by a schema and its parser.
const QStringList &arrangeStepFields()
{
    static const QStringList fields{QStringLiteral("action"), QStringLiteral("area"), QStringLiteral("change"), QStringLiteral("to"), QStringLiteral("name")};
    return fields;
}

const QStringList &composeSlotFields()
{
    static const QStringList fields{QStringLiteral("name"), QStringLiteral("holds"), QStringLiteral("weight"), QStringLiteral("position"), QStringLiteral("items")};
    return fields;
}

// "none" and every template id, in catalog order.
const QStringList &composeTemplateChoices()
{
    static const QStringList choices = []() {
        QStringList result{QStringLiteral("none")};
        for (const Workspace::Compose::Template &recipe : Workspace::Compose::templates()) {
            result.append(recipe.id);
        }
        return result;
    }();
    return choices;
}

// The Library a file reference belongs to: this chat's, or another by id or
// name (a worktree is a Library of its own).
QVariantMap resolveLibrary(CoreDatabase &database, const Context &context, const QString &reference)
{
    const QString wanted = JsCompat::trimmed(reference);
    const QVariantList libraries = LibraryRepository::activeLibraries(database);

    if (wanted.isEmpty()) {
        if (context.libraryId.isEmpty()) {
            throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("This chat has no Library; name the Library the file is in."));
        }
        for (const QVariant &value : libraries) {
            if (value.toMap().value(QStringLiteral("id")).toString() == context.libraryId) {
                return value.toMap();
            }
        }
        return {{QStringLiteral("id"), context.libraryId}};
    }

    QVariantMap partial;
    for (const QVariant &value : libraries) {
        const QVariantMap library = value.toMap();
        const QString id = library.value(QStringLiteral("id")).toString();
        const QString name = library.value(QStringLiteral("name")).toString();
        if (id == wanted || name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return library;
        }
        if (partial.isEmpty() && wanted.size() >= 3 && name.contains(wanted, Qt::CaseInsensitive)) {
            partial = library;
        }
    }

    if (!partial.isEmpty()) {
        return partial;
    }

    QStringList names;
    for (const QVariant &value : libraries) {
        names.append(value.toMap().value(QStringLiteral("name")).toString());
    }
    throw ToolError(
        QStringLiteral("invalid_input"),
        QStringLiteral("No Library is called \"%1\". Libraries: %2.").arg(wanted, names.join(QStringLiteral(", ")))
    );
}

QVariantMap callWorkspace(Context &context, QVariantMap request)
{
    request.insert(QStringLiteral("runId"), context.runId);
    request.insert(QStringLiteral("chatId"), context.chatId);
    request.insert(QStringLiteral("explicit"), context.workspaceExplicit);
    request.insert(QStringLiteral("asked"), context.workspaceAsked);
    const QVariantMap result = context.workspace ? context.workspace(request) : Workspace::Gateway::call(request);

    if (!result.value(QStringLiteral("ok")).toBool()) {
        const QString code = result.value(QStringLiteral("code")).toString();
        const QString message = result.value(QStringLiteral("error")).toString();
        const QString toolCode = code == QStringLiteral("LAYOUT_LOCKED") || code == QStringLiteral("CONTENT_LOCKED")
                || code == QStringLiteral("UNLOCK_REFUSED") || code == QStringLiteral("NOT_ALLOWED")
            ? QStringLiteral("permission_denied")
            : code == QStringLiteral("NOT_FOUND") || code == QStringLiteral("INVALID_ARGUMENT") || code == QStringLiteral("UNSUPPORTED")
            ? QStringLiteral("invalid_input")
            : QStringLiteral("execution_failed");
        throw ToolError(toolCode, message.isEmpty() ? QStringLiteral("The workspace change failed.") : message, {{QStringLiteral("workspaceCode"), code}});
    }

    QVariantMap output = result;
    output.remove(QStringLiteral("ok"));
    return output;
}

QVariantMap workspaceViewTool(const QVariantMap &, Context &context)
{
    return callWorkspace(context, {{QStringLiteral("action"), QStringLiteral("describe")}});
}

// A tool item (file by path, chat, terminal, open view) in workspace terms.
// Files are looked up here, where the Library is.
QVariantMap resolveShowItem(const QVariantMap &item, Context &context)
{
    const QString kind = item.value(QStringLiteral("kind")).toString();
    const QString reference = item.value(QStringLiteral("reference")).toString();

    if (kind == QStringLiteral("file")) {
        if (JsCompat::isBlank(reference)) {
            throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("A file needs its Library path, file name or file id."));
        }
        // Looked up in the catalog here; the workspace then prefers a view
        // that already shows it (it may be open from elsewhere), so the
        // lookup's verdict travels along instead of failing the call.
        const QVariantMap library = resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString());
        const QString libraryId = library.value(QStringLiteral("id")).toString();
        QVariantMap resolved{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("reference"), reference},
            {QStringLiteral("libraryId"), libraryId},
        };
        const QString wanted = QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/')).trimmed();
        const QString lowered = wanted.toLower();
        QVariantList exact;
        QVariantList byName;
        for (const QVariant &value : LibraryFileRepository::files(*context.database, libraryId)) {
            const QVariantMap file = value.toMap();
            const QString path = file.value(QStringLiteral("relativePath")).toString();
            if (file.value(QStringLiteral("id")).toString() == wanted || path == wanted) {
                exact.append(file);
            } else if (path.toLower() == lowered || path.toLower().endsWith(QLatin1Char('/') + lowered)
                       || file.value(QStringLiteral("name")).toString().toLower() == lowered) {
                byName.append(file);
            }
        }
        const QVariantList &matches = exact.isEmpty() ? byName : exact;
        if (matches.size() == 1) {
            const QString fileId = matches.first().toMap().value(QStringLiteral("id")).toString();
            const auto file = LibraryFileRepository::fileById(*context.database, libraryId, fileId);
            resolved.insert(QStringLiteral("lookup"), file ? QStringLiteral("found") : QStringLiteral("not_found"));
            if (file) {
                resolved.insert(QStringLiteral("file"), *file);
            }
        } else if (matches.size() > 1) {
            QStringList candidates;
            for (const QVariant &match : matches.mid(0, 5)) {
                candidates.append(match.toMap().value(QStringLiteral("relativePath")).toString());
            }
            resolved.insert(QStringLiteral("lookup"), QStringLiteral("ambiguous"));
            resolved.insert(QStringLiteral("candidates"), candidates);
        } else {
            resolved.insert(QStringLiteral("lookup"), QStringLiteral("not_found"));
        }
        return resolved;
    }

    QVariantMap resolved{{QStringLiteral("kind"), kind}, {QStringLiteral("reference"), reference}};
    if (kind == QStringLiteral("terminal") && !JsCompat::isBlank(item.value(QStringLiteral("library")).toString())) {
        resolved.insert(QStringLiteral("libraryId"), resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString()).value(QStringLiteral("id")));
    }
    return resolved;
}

QVariantMap workspaceShowTool(const QVariantMap &input, Context &context)
{
    QVariantList items;
    for (const QVariant &value : input.value(QStringLiteral("items")).toList()) {
        items.append(resolveShowItem(value.toMap(), context));
    }

    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("show")},
        {QStringLiteral("items"), items},
        {QStringLiteral("where"), input.value(QStringLiteral("where"))},
        {QStringLiteral("purpose"), input.value(QStringLiteral("purpose"))},
        {QStringLiteral("focus"), input.value(QStringLiteral("focus"))},
    });
}

QVariantMap workspaceArrangeTool(const QVariantMap &input, Context &context)
{
    QVariantList steps;
    for (const QVariant &value : input.value(QStringLiteral("steps")).toList()) {
        const QVariantMap step = value.toMap();
        steps.append(QVariantMap{
            {QStringLiteral("action"), step.value(QStringLiteral("action"))},
            {QStringLiteral("target"), step.value(QStringLiteral("area"))},
            {QStringLiteral("change"), step.value(QStringLiteral("change"))},
            {QStringLiteral("to"), step.value(QStringLiteral("to"))},
            {QStringLiteral("name"), step.value(QStringLiteral("name"))},
        });
    }

    // One step goes as itself; several go as one change that undoes together.
    QVariantMap request = steps.size() == 1
        ? steps.first().toMap()
        : QVariantMap{{QStringLiteral("action"), QStringLiteral("steps")}, {QStringLiteral("steps"), steps}};
    request.insert(QStringLiteral("saveAs"), input.value(QStringLiteral("save_as")));
    return callWorkspace(context, request);
}

QVariantMap workspaceComposeTool(const QVariantMap &input, Context &context)
{
    // Everything that can be found goes in; the rest is reported back.
    QStringList unresolved;
    QVariantList panelSlots;
    for (const QVariant &value : input.value(QStringLiteral("slots")).toList()) {
        QVariantMap slot = value.toMap();
        QVariantList items;
        for (const QVariant &item : slot.value(QStringLiteral("items")).toList()) {
            try {
                items.append(resolveShowItem(item.toMap(), context));
            } catch (const ToolError &error) {
                unresolved.append(error.message());
            }
        }
        slot.insert(QStringLiteral("items"), items);
        panelSlots.append(slot);
    }

    const QString templateId = input.value(QStringLiteral("template")).toString();
    const QString strategy = input.value(QStringLiteral("arrangement")).toString();
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("compose")},
        {QStringLiteral("mode"), input.value(QStringLiteral("mode"))},
        {QStringLiteral("template"), templateId == QStringLiteral("none") ? QString() : templateId},
        {QStringLiteral("strategy"), strategy == QStringLiteral("auto") ? QString() : strategy},
        {QStringLiteral("rows"), input.value(QStringLiteral("rows"))},
        {QStringLiteral("columns"), input.value(QStringLiteral("columns"))},
        {QStringLiteral("slots"), panelSlots},
        {QStringLiteral("focus"), input.value(QStringLiteral("focus"))},
        {QStringLiteral("saveAs"), input.value(QStringLiteral("save_as"))},
        {QStringLiteral("unresolved"), unresolved},
    });
}

QVariantMap workspaceRestoreTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("restore")},
        {QStringLiteral("scope"), input.value(QStringLiteral("scope"))},
        {QStringLiteral("layout"), input.value(QStringLiteral("layout"))},
    });
}

QVariantMap workspaceLayoutTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("layout")},
        {QStringLiteral("op"), input.value(QStringLiteral("action"))},
        {QStringLiteral("name"), input.value(QStringLiteral("name"))},
        {QStringLiteral("newName"), input.value(QStringLiteral("new_name"))},
        {QStringLiteral("replace"), input.value(QStringLiteral("replace"))},
    });
}

QVariantMap workspacePreferenceTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("preference")},
        {QStringLiteral("op"), input.value(QStringLiteral("action"))},
        {QStringLiteral("preference"), input.value(QStringLiteral("preference"))},
        {QStringLiteral("value"), input.value(QStringLiteral("value"))},
        {QStringLiteral("area"), input.value(QStringLiteral("area"))},
        {QStringLiteral("lasting"), input.value(QStringLiteral("duration")).toString() == QStringLiteral("always")},
        {QStringLiteral("lastingAllowed"), context.workspaceLastingPreference},
    });
}

QVariantMap workspaceOutputSummary(const QVariantMap &output)
{
    return {
        {QStringLiteral("status"), output.value(QStringLiteral("status"))},
        {QStringLiteral("notes"), output.value(QStringLiteral("notes"))},
    };
}
} // namespace

namespace AITools
{
QList<Definition> workspaceTools()
{
    QList<Definition> tools;
    const QString readOnly = readOnlyPermission;
    const QString mutation = safeLocalMutationPermission;

    const QString areaDescription = QStringLiteral(
        "An area from workspace_view (its name or id), or a plain description: \"the code\", \"the chat\", \"the documentation\", \"the right panel\", \"this\"."
    );

    tools.append({
        QStringLiteral("workspace_view"),
        QStringLiteral("View Workspace"),
        QStringLiteral("Describe the person's workspace: each area (panel) with its name, position, size, what it shows and its locks, where the conversation is, whether a temporary arrangement can be undone, and the person's workspace preferences. Usually not needed: a summary is in your instructions when the person asks for a change."),
        readOnly, workspaceMode,
        objectSchema({}, {}),
        [](const QVariant &input) { return strictObject(input, {}); },
        workspaceViewTool,
        [](const QVariantMap &) { return QVariantMap(); },
        [](const QVariantMap &output) {
            return QVariantMap{{QStringLiteral("areaCount"), output.value(QStringLiteral("areas")).toList().size()}};
        },
    });

    const QVariantMap showItem = objectSchema(
        {
            {QStringLiteral("kind"), enumSchema(showKinds(), QStringLiteral("file: a Library file. chat: a conversation (\"\" for this one). terminal: \"\" for the terminal, \"new\" for another. diff: changes under review. open_view: something already open, by view id or title."))},
            {QStringLiteral("reference"), stringSchema(0, 1024, QStringLiteral("file: Library-relative path or file id. chat: chat id or \"\". terminal: \"\" or \"new\". open_view: view id or title."))},
            {QStringLiteral("library"), stringSchema(0, 256, QStringLiteral("\"\" for this chat's Library, else a Library or worktree name or id. Files keep the Library they belong to."))},
        },
        {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")}
    );

    tools.append({
        QStringLiteral("workspace_show"),
        QStringLiteral("Show in Workspace"),
        QStringLiteral("Open or bring forward files, chats or terminals in the workspace. Things already open are reused, not duplicated; several files share one area as tabs. Put every item in one call and leave where empty unless the person said where. Only call this when the person asked to see, open or bring something up; to find which files matter, use Library evidence first and show two to five."),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("items"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), showItem}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 8}}},
                {QStringLiteral("where"), stringSchema(0, 200, QStringLiteral("\"\" to choose, or e.g. \"right\", \"left\", \"bottom\", \"beside the chat\", an area name, \"new panel\"."))},
                {QStringLiteral("purpose"), stringSchema(0, 64, QStringLiteral("\"\", or what the items are for: \"documentation\", \"reference\", \"code\"."))},
                {QStringLiteral("focus"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Move keyboard focus there. Only when the person asked to switch to it.")}}},
            },
            {QStringLiteral("items"), QStringLiteral("where"), QStringLiteral("purpose"), QStringLiteral("focus")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("items"), QStringLiteral("where"), QStringLiteral("purpose"), QStringLiteral("focus")});
            const QVariant itemsValue = map.value(QStringLiteral("items"));
            if (itemsValue.typeId() != QMetaType::QVariantList) {
                invalid(QStringLiteral("items"), QStringLiteral("Expected array."));
            }
            const QVariantList list = itemsValue.toList();
            if (list.isEmpty() || list.size() > 8) {
                invalid(QStringLiteral("items"), QStringLiteral("Show between one and eight items."));
            }
            QVariantList items;
            for (const QVariant &value : list) {
                const QVariantMap item = strictObject(value, {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")});
                items.append(QVariantMap{
                    {QStringLiteral("kind"), enumField(item, QStringLiteral("kind"), showKinds())},
                    {QStringLiteral("reference"), stringField(item, QStringLiteral("reference"), 0, 1024, true, QString())},
                    {QStringLiteral("library"), stringField(item, QStringLiteral("library"), 0, 256, true, QString())},
                });
            }
            return QVariantMap{
                {QStringLiteral("items"), items},
                {QStringLiteral("where"), stringField(map, QStringLiteral("where"), 0, 200, true, QString())},
                {QStringLiteral("purpose"), stringField(map, QStringLiteral("purpose"), 0, 64, true, QString())},
                {QStringLiteral("focus"), map.contains(QStringLiteral("focus")) ? boolField(map, QStringLiteral("focus")) : false},
            };
        },
        workspaceShowTool,
        [](const QVariantMap &input) {
            QStringList references;
            for (const QVariant &item : input.value(QStringLiteral("items")).toList()) {
                references.append(item.toMap().value(QStringLiteral("kind")).toString() + QLatin1Char(':') + item.toMap().value(QStringLiteral("reference")).toString());
            }
            return QVariantMap{{QStringLiteral("items"), references}, {QStringLiteral("where"), input.value(QStringLiteral("where"))}};
        },
        workspaceOutputSummary,
    });

    const QStringList &stepFields = arrangeStepFields();
    const QVariantMap arrangeStep = objectSchema(
        {
            {QStringLiteral("action"), enumSchema(arrangeActions())},
            {QStringLiteral("area"), stringSchema(0, 200, areaDescription)},
            {QStringLiteral("change"), stringSchema(0, 32, QStringLiteral("resize only, else \"\"."))},
            {QStringLiteral("to"), stringSchema(0, 200, QStringLiteral("move, add and remove only, else \"\"."))},
            {QStringLiteral("name"), stringSchema(0, 80, QStringLiteral("label and add only, else \"\"."))},
        },
        stepFields
    );

    tools.append({
        QStringLiteral("workspace_arrange"),
        QStringLiteral("Arrange Workspace"),
        QStringLiteral("Small edits to the panels that exist: steps run in order as one change, and nothing applies unless every step works. For any arrangement of several panels (grids, columns, \"4 panes\", a new layout) use workspace_compose instead; at most one add per call. Step actions: focus (bring an area forward), resize (change: larger, smaller, maximize, restore, equal, or a share like 0.6), move (to: left, right, top, bottom, \"beside <area>\", or another area), keep (lock it where it is), label (name: what the person calls it), add (a new empty panel: to = side, area = the area it goes beside or \"\" for the window edge, name = optional), remove (take a panel away; its tabs move to the area in to, else the main editor; nothing closes). save_as: after the steps, save the result as a new named layout (\"\" to not save). Locked areas refuse changes; never try to unlock one."),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("steps"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), arrangeStep}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 12}}},
                {QStringLiteral("save_as"), stringSchema(0, 80, QStringLiteral("A new layout name to save the result as, else \"\"."))},
            },
            {QStringLiteral("steps"), QStringLiteral("save_as")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("steps"), QStringLiteral("save_as")});
            const QVariant stepsValue = map.value(QStringLiteral("steps"));
            if (stepsValue.typeId() != QMetaType::QVariantList) {
                invalid(QStringLiteral("steps"), QStringLiteral("Expected array."));
            }
            const QVariantList list = stepsValue.toList();
            if (list.isEmpty() || list.size() > 12) {
                invalid(QStringLiteral("steps"), QStringLiteral("Give between one and twelve steps."));
            }
            QVariantList steps;
            for (const QVariant &value : list) {
                const QVariantMap step = strictObject(value, arrangeStepFields());
                steps.append(QVariantMap{
                    {QStringLiteral("action"), enumField(step, QStringLiteral("action"), arrangeActions())},
                    {QStringLiteral("area"), stringField(step, QStringLiteral("area"), 0, 200, true, QString())},
                    {QStringLiteral("change"), stringField(step, QStringLiteral("change"), 0, 32, true, QString())},
                    {QStringLiteral("to"), stringField(step, QStringLiteral("to"), 0, 200, true, QString())},
                    {QStringLiteral("name"), stringField(step, QStringLiteral("name"), 0, 80, true, QString())},
                });
            }
            // Building an arrangement from adds and moves is what
            // produced stacks of strips; that's workspace_compose's job.
            qsizetype adds = 0;
            for (const QVariant &step : std::as_const(steps)) {
                adds += step.toMap().value(QStringLiteral("action")).toString() == QStringLiteral("add") ? 1 : 0;
            }
            if (adds > 1) {
                invalid(QStringLiteral("steps"), QStringLiteral("Use workspace_compose (for example arrangement grid) to set up several panels; arrange adds at most one."));
            }
            return QVariantMap{
                {QStringLiteral("steps"), steps},
                {QStringLiteral("save_as"), stringField(map, QStringLiteral("save_as"), 0, 80, true, QString())},
            };
        },
        workspaceArrangeTool,
        [](const QVariantMap &input) {
            QStringList actions;
            for (const QVariant &step : input.value(QStringLiteral("steps")).toList()) {
                actions.append(step.toMap().value(QStringLiteral("action")).toString());
            }
            return QVariantMap{{QStringLiteral("steps"), actions}, {QStringLiteral("saveAs"), input.value(QStringLiteral("save_as"))}};
        },
        workspaceOutputSummary,
    });

    const QStringList &slotFields = composeSlotFields();
    const QVariantMap composeSlot = objectSchema(
        {
            {QStringLiteral("name"), stringSchema(1, 40, QStringLiteral("What the panel is called: \"Code\", \"Docs\", \"Chat\"."))},
            {QStringLiteral("holds"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), enumSchema(Workspace::Compose::holdWords())}, {QStringLiteral("maxItems"), 4}, {QStringLiteral("description"), QStringLiteral("What kind of content belongs there; can be empty when items say it.")}}},
            {QStringLiteral("weight"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10}, {QStringLiteral("description"), QStringLiteral("Relative room: 1 normal, 2 about twice; 0 keeps the default.")}}},
            {QStringLiteral("position"), stringSchema(0, 24, QStringLiteral("Where it goes. grid: top-left, top-right, bottom-left, bottom-right, top, bottom-center, r2c1. columns: left, center, right. rows: top, middle, bottom. split: primary for the large one; the first other panel's right/left/top/bottom says the side. "": the next free place."))},
            {QStringLiteral("items"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), showItem}, {QStringLiteral("maxItems"), Workspace::Compose::kMaximumItemsPerSlot}}},
        },
        slotFields
    );
    const QStringList &templateChoices = composeTemplateChoices();
    QStringList templateLines;
    for (const Workspace::Compose::Template &recipe : Workspace::Compose::templates()) {
        QStringList slotNames;
        for (const QJsonValue &slot : recipe.recipe.value(QStringLiteral("slots")).toArray()) {
            slotNames.append(slot.toObject().value(QStringLiteral("name")).toString());
        }
        templateLines.append(QStringLiteral("%1 (%2: %3)").arg(recipe.id, recipe.description.chopped(1), slotNames.join(QStringLiteral(", "))));
    }

    tools.append({
        QStringLiteral("workspace_compose"),
        QStringLiteral("Compose Workspace"),
        QStringLiteral("Set the workspace to the state the person asked for, in one call. You only choose; Archivist finds the files, builds and sizes the panels, reuses what is open and applies it as one change the person can undo or redo. Never build layouts from moves or splits. mode patch (the usual case): the current panels stay where they are; a slot named like an existing panel gets its items there, a new name adds one panel at its position (left, right, top or bottom), and panels you don't name are left alone. Use it for \"put the PDF beside the chat\" or \"add my notes on the right\": arrangement auto, rows and columns 0. mode replace: a whole new arrangement from a recipe, only when the person asks for one (\"4 panes\", \"three columns\", \"chat dominant\", a template). Recipes: grid (rows x columns; \"4 panes, chat bottom-left\" is grid, rows 2, columns 2, chat at bottom-left; empty places become panels showing what was open), columns (left, center, right), rows (top, middle, bottom), split (one large panel at position primary, the rest stacked on one side: \"chat dominant, three images stacked on the right\"), stack (everything as tabs in one panel). Weights are relative (1 normal, 2 about twice); panels that don't fit become tabs. Files can be named by file name, path or an open tab's title; nothing closes. Templates fill their panels by name: %1. save_as also saves it as a named layout (\"\" keeps it temporary). focus: a panel name to bring forward, only when asked.").arg(templateLines.join(QStringLiteral("; "))),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("mode"), enumSchema(QStringList{QStringLiteral("patch"), QStringLiteral("replace")})},
                {QStringLiteral("template"), enumSchema(templateChoices)},
                {QStringLiteral("arrangement"), enumSchema(QStringList{QStringLiteral("auto")} + Workspace::Compose::strategies())},
                {QStringLiteral("rows"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}, {QStringLiteral("description"), QStringLiteral("grid only, else 0.")}}},
                {QStringLiteral("columns"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}, {QStringLiteral("description"), QStringLiteral("grid only, else 0.")}}},
                {QStringLiteral("slots"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), composeSlot}, {QStringLiteral("maxItems"), Workspace::Compose::kMaximumSlots}}},
                {QStringLiteral("focus"), stringSchema(0, 40, QStringLiteral("A panel name, or \"\"."))},
                {QStringLiteral("save_as"), stringSchema(0, 80, QStringLiteral("A new layout name, or \"\"."))},
            },
            {QStringLiteral("mode"), QStringLiteral("template"), QStringLiteral("arrangement"), QStringLiteral("rows"), QStringLiteral("columns"), QStringLiteral("slots"), QStringLiteral("focus"), QStringLiteral("save_as")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("mode"), QStringLiteral("template"), QStringLiteral("arrangement"), QStringLiteral("rows"), QStringLiteral("columns"), QStringLiteral("slots"), QStringLiteral("focus"), QStringLiteral("save_as")});
            const QVariant slotsValue = map.value(QStringLiteral("slots"));
            if (slotsValue.typeId() != QMetaType::QVariantList) {
                invalid(QStringLiteral("slots"), QStringLiteral("Expected array."));
            }
            const QVariantList list = slotsValue.toList();
            if (list.size() > Workspace::Compose::kMaximumSlots) {
                invalid(QStringLiteral("slots"), QStringLiteral("At most %1 panels.").arg(Workspace::Compose::kMaximumSlots));
            }
            QVariantList panelSlots;
            for (const QVariant &value : list) {
                const QVariantMap slot = strictObject(value, composeSlotFields());
                QStringList holds;
                const QVariantList holdList = slot.value(QStringLiteral("holds")).toList();
                if (holdList.size() > 4) {
                    invalid(QStringLiteral("holds"), QStringLiteral("At most four kinds."));
                }
                for (const QVariant &hold : holdList) {
                    const QString word = hold.toString();
                    if (!Workspace::Compose::holdWords().contains(word)) {
                        invalid(QStringLiteral("holds"), QStringLiteral("Use: %1.").arg(Workspace::Compose::holdWords().join(QStringLiteral(", "))));
                    }
                    holds.append(word);
                }
                const QVariantList itemList = slot.value(QStringLiteral("items")).toList();
                if (itemList.size() > Workspace::Compose::kMaximumItemsPerSlot) {
                    invalid(QStringLiteral("items"), QStringLiteral("At most %1 items per panel.").arg(Workspace::Compose::kMaximumItemsPerSlot));
                }
                QVariantList items;
                for (const QVariant &itemValue : itemList) {
                    const QVariantMap item = strictObject(itemValue, {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")});
                    items.append(QVariantMap{
                        {QStringLiteral("kind"), enumField(item, QStringLiteral("kind"), showKinds())},
                        {QStringLiteral("reference"), stringField(item, QStringLiteral("reference"), 0, 1024, true, QString())},
                        {QStringLiteral("library"), stringField(item, QStringLiteral("library"), 0, 256, true, QString())},
                    });
                }
                panelSlots.append(QVariantMap{
                    {QStringLiteral("name"), stringField(slot, QStringLiteral("name"), 1, 40, true)},
                    {QStringLiteral("holds"), holds},
                    {QStringLiteral("weight"), intField(slot, QStringLiteral("weight"), 0, 10, 0)},
                    {QStringLiteral("position"), slot.contains(QStringLiteral("position")) ? stringField(slot, QStringLiteral("position"), 0, 24, true, QString()) : QString()},
                    {QStringLiteral("items"), items},
                });
            }
            return QVariantMap{
                {QStringLiteral("mode"), map.contains(QStringLiteral("mode")) ? enumField(map, QStringLiteral("mode"), QStringList{QStringLiteral("patch"), QStringLiteral("replace")}) : QString()},
                {QStringLiteral("template"), enumField(map, QStringLiteral("template"), composeTemplateChoices())},
                {QStringLiteral("arrangement"), enumField(map, QStringLiteral("arrangement"), QStringList{QStringLiteral("auto")} + Workspace::Compose::strategies())},
                {QStringLiteral("rows"), map.contains(QStringLiteral("rows")) ? intField(map, QStringLiteral("rows"), 0, 3, 0) : 0},
                {QStringLiteral("columns"), map.contains(QStringLiteral("columns")) ? intField(map, QStringLiteral("columns"), 0, 3, 0) : 0},
                {QStringLiteral("slots"), panelSlots},
                {QStringLiteral("focus"), stringField(map, QStringLiteral("focus"), 0, 40, true, QString())},
                {QStringLiteral("save_as"), stringField(map, QStringLiteral("save_as"), 0, 80, true, QString())},
            };
        },
        workspaceComposeTool,
        [](const QVariantMap &input) {
            QStringList names;
            for (const QVariant &slot : input.value(QStringLiteral("slots")).toList()) {
                names.append(slot.toMap().value(QStringLiteral("name")).toString());
            }
            return QVariantMap{{QStringLiteral("template"), input.value(QStringLiteral("template"))}, {QStringLiteral("panels"), names}};
        },
        [](const QVariantMap &output) {
            QVariantMap summary = workspaceOutputSummary(output);
            if (output.contains(QStringLiteral("unresolved"))) {
                summary.insert(QStringLiteral("unresolved"), output.value(QStringLiteral("unresolved")));
            }
            return summary;
        },
    });

    tools.append({
        QStringLiteral("workspace_restore"),
        QStringLiteral("Restore Workspace"),
        QStringLiteral("Put the workspace back. before_task: how it was before you started rearranging (\"put everything back\", \"restore my workspace\", \"put it back\"). last_change: undo the most recent workspace change (\"undo that\", \"switch back\" after a change). redo: redo the change just undone (\"redo\", \"actually, put it back how you had it\"). saved_layout: apply one of the person's saved layouts by name. Open files, chats and terminals stay open; unsaved work is never closed."),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("scope"), enumSchema(restoreScopes())},
                {QStringLiteral("layout"), stringSchema(0, 120, QStringLiteral("saved_layout only: the layout's name, else \"\"."))},
            },
            {QStringLiteral("scope"), QStringLiteral("layout")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("scope"), QStringLiteral("layout")});
            return QVariantMap{
                {QStringLiteral("scope"), enumField(map, QStringLiteral("scope"), restoreScopes())},
                {QStringLiteral("layout"), stringField(map, QStringLiteral("layout"), 0, 120, true, QString())},
            };
        },
        workspaceRestoreTool,
        [](const QVariantMap &input) { return QVariantMap{{QStringLiteral("scope"), input.value(QStringLiteral("scope"))}}; },
        workspaceOutputSummary,
    });

    tools.append({
        QStringLiteral("workspace_layout"),
        QStringLiteral("Workspace Layouts"),
        QStringLiteral("Named layouts: whole arrangements of panels the person can switch between. list shows the built-in and saved ones. apply switches to one by name (open tabs move into it; nothing closes; undoable); \"try another\" means apply a different one than the current layout. save stores the current arrangement under name (workspace_arrange save_as does this in the same call as the arranging). Saving over an existing name needs replace true, and only when the person said to replace it. rename (new_name) and delete work on the person's saved layouts only, and only when they ask."),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("action"), enumSchema(layoutOps())},
                {QStringLiteral("name"), stringSchema(0, 80, QStringLiteral("The layout's name; \"\" for list."))},
                {QStringLiteral("new_name"), stringSchema(0, 80, QStringLiteral("rename only, else \"\"."))},
                {QStringLiteral("replace"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("save only: replace a saved layout with the same name.")}}},
            },
            {QStringLiteral("action"), QStringLiteral("name"), QStringLiteral("new_name"), QStringLiteral("replace")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("action"), QStringLiteral("name"), QStringLiteral("new_name"), QStringLiteral("replace")});
            return QVariantMap{
                {QStringLiteral("action"), enumField(map, QStringLiteral("action"), layoutOps())},
                {QStringLiteral("name"), stringField(map, QStringLiteral("name"), 0, 80, true, QString())},
                {QStringLiteral("new_name"), stringField(map, QStringLiteral("new_name"), 0, 80, true, QString())},
                {QStringLiteral("replace"), boolField(map, QStringLiteral("replace"))},
            };
        },
        workspaceLayoutTool,
        [](const QVariantMap &input) {
            return QVariantMap{{QStringLiteral("action"), input.value(QStringLiteral("action"))}, {QStringLiteral("name"), input.value(QStringLiteral("name"))}};
        },
        workspaceOutputSummary,
    });

    tools.append({
        QStringLiteral("workspace_preference"),
        QStringLiteral("Workspace Preference"),
        QStringLiteral("Remember how the person likes their workspace, only when they state a preference (not for a one-off request). chat_location: bottom | docked (\"I like my chat at the bottom\"). main_editor: large | normal (\"keep my main editor large\"). rearranging: only_when_asked | when_helpful (\"don't rearrange my windows unless I ask\"). area_purpose: value is what a panel is for, e.g. documentation, and area names the panel (\"use the right panel for documentation\"). duration: always only when they say always, from now on, by default or describe a standing habit; otherwise this_session. clear forgets one; list shows them."),
        mutation, workspaceMode,
        objectSchema(
            {
                {QStringLiteral("action"), enumSchema(preferenceOps())},
                {QStringLiteral("preference"), enumSchema(preferenceKeys())},
                {QStringLiteral("value"), stringSchema(0, 40, QStringLiteral("set: the value; clear: \"\" (or the purpose for area_purpose)."))},
                {QStringLiteral("area"), stringSchema(0, 200, QStringLiteral("area_purpose only: the panel, e.g. \"the right panel\" or \"this panel\"; else \"\"."))},
                {QStringLiteral("duration"), enumSchema(preferenceDurations())},
            },
            {QStringLiteral("action"), QStringLiteral("preference"), QStringLiteral("value"), QStringLiteral("area"), QStringLiteral("duration")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("action"), QStringLiteral("preference"), QStringLiteral("value"), QStringLiteral("area"), QStringLiteral("duration")});
            return QVariantMap{
                {QStringLiteral("action"), enumField(map, QStringLiteral("action"), preferenceOps())},
                {QStringLiteral("preference"), enumField(map, QStringLiteral("preference"), preferenceKeys())},
                {QStringLiteral("value"), stringField(map, QStringLiteral("value"), 0, 40, true, QString())},
                {QStringLiteral("area"), stringField(map, QStringLiteral("area"), 0, 200, true, QString())},
                {QStringLiteral("duration"), enumField(map, QStringLiteral("duration"), preferenceDurations())},
            };
        },
        workspacePreferenceTool,
        [](const QVariantMap &input) {
            return QVariantMap{{QStringLiteral("action"), input.value(QStringLiteral("action"))}, {QStringLiteral("preference"), input.value(QStringLiteral("preference"))}, {QStringLiteral("duration"), input.value(QStringLiteral("duration"))}};
        },
        workspaceOutputSummary,
    });

    return tools;
}
} // namespace AITools
