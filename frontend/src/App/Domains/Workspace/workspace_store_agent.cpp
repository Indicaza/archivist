// WorkspaceStore: what agents may do to the workspace, and when.
//
// The rules (who may change what) are the model's; the vocabulary ("the
// code", "on the right") is Workspace::Intents'. This file decides when a
// request runs: now, or later as a suggestion the person applies, and keeps
// every agent change undoable as one step.

#include "App/Domains/Workspace/workspace_store.h"

#include "App/Domains/Documents/document_store.h"
#include "Core/Domains/Workspace/workspace_arrangements.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_compose.h"
#include "Core/Domains/Workspace/workspace_presets.h"
#include "Core/Domains/Workspace/workspace_resources.h"

#include <QDateTime>
#include <algorithm>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

using namespace Workspace;

namespace
{
constexpr qsizetype kMaximumHistorySteps = 20;
constexpr qsizetype kMaximumAgentLog = 50;
// After a key press or click, agents leave focus alone for this long.
constexpr qint64 kUserBusyMs = 1500;

QVariantMap failure(const QString &code, const QString &message)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("code"), code},
        {QStringLiteral("error"), message},
    };
}

QString summaryOf(const QStringList &notes)
{
    return notes.join(QStringLiteral(". "));
}
} // namespace

qint64 WorkspaceStore::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

void WorkspaceStore::noteUserInput()
{
    m_lastUserInput = now();
}

bool WorkspaceStore::userBusy() const
{
    return m_lastUserInput > 0 && now() - m_lastUserInput < kUserBusyMs;
}

bool WorkspaceStore::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::InputMethod:
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
        noteUserInput();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void WorkspaceStore::setCommandSurfaceState(bool collapsed, const QString &view)
{
    const QString next = view == QStringLiteral("terminal") || view == QStringLiteral("problems") ? view : QStringLiteral("chat");
    if (collapsed == m_commandSurfaceCollapsed && next == m_commandSurfaceView) {
        return;
    }
    m_commandSurfaceCollapsed = collapsed;
    m_commandSurfaceView = next;
    emit commandSurfaceChanged();
}

QVariantMap WorkspaceStore::commandSurface() const
{
    return {
        {QStringLiteral("collapsed"), m_commandSurfaceCollapsed},
        {QStringLiteral("view"), m_commandSurfaceView},
    };
}

void WorkspaceStore::noteFocusHistory()
{
    const QString focused = m_model.focusedAnchorId();
    if (focused.isEmpty() || m_recentAnchors.value(0) == focused) {
        return;
    }
    m_recentAnchors.removeAll(focused);
    m_recentAnchors.prepend(focused);
    // Forget panels that are gone, and keep the list short.
    for (qsizetype index = m_recentAnchors.size() - 1; index >= 0; --index) {
        if (!m_model.anchor(m_recentAnchors.at(index)) || index >= 12) {
            m_recentAnchors.removeAt(index);
        }
    }
}

Intents::Environment WorkspaceStore::agentEnvironment(bool mayMoveFocus) const
{
    Intents::Environment environment;
    environment.registry = &m_registry;
    environment.metrics = m_metrics;
    environment.origin = Origin::Agent;
    environment.mayMoveFocus = mayMoveFocus;
    environment.recentAnchors = m_recentAnchors;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        environment.surfaceSizes.insert(surfaceId, surfaceSize(surfaceId));
    }
    const Preferences::Effects effects = preferenceEffects();
    environment.mayAddAreas = !effects.onlyWhenAsked;
    if (effects.keepMainLarge) {
        environment.sidePanelShare = 0.28;
        environment.largestSideShare = 0.45;
    }
    environment.purposeAreas = effects.purposeAreas;
    return environment;
}

QVariantMap WorkspaceStore::describeForAgent() const
{
    QVariantMap description = Intents::describe(m_model, agentEnvironment(false));
    description.insert(QStringLiteral("bottomPanel"), QVariantMap{
        {QStringLiteral("shows"), m_commandSurfaceView},
        {QStringLiteral("collapsed"), m_commandSurfaceCollapsed},
    });
    description.insert(QStringLiteral("canUndoLastChange"), canUndoAgentChange());
    QStringList preferences;
    for (const QVariant &preference : workspacePreferences()) {
        const QVariantMap map = preference.toMap();
        preferences.append(map.value(QStringLiteral("sentence")).toString() + QStringLiteral(" (") + map.value(QStringLiteral("duration")).toString() + QLatin1Char(')'));
    }
    if (!preferences.isEmpty()) {
        description.insert(QStringLiteral("preferences"), preferences);
    }
    if (!m_baseline.isEmpty()) {
        // "Put everything back" (before_task) is available, even after a restart.
        description.insert(QStringLiteral("temporaryArrangementSince"), m_baselineSince);
    }
    if (!m_suggestion.isEmpty()) {
        description.insert(QStringLiteral("pendingSuggestion"), m_suggestion.value(QStringLiteral("summary")));
    }
    QVariantList layouts;
    for (const QVariant &layout : savedLayouts()) {
        layouts.append(layout.toMap().value(QStringLiteral("name")));
    }
    if (!layouts.isEmpty()) {
        description.insert(QStringLiteral("savedLayouts"), layouts);
    }
    // Every layout that can be switched to, the person's and the built-in ones.
    description.insert(QStringLiteral("layouts"), layoutsForAgent());
    // The arrangement last composed, while its panels are still there, so
    // a follow-up can change just what was asked.
    if (!m_lastComposition.isEmpty()) {
        bool present = false;
        for (const QVariant &panel : m_lastComposition.value(QStringLiteral("panels")).toList()) {
            for (const QString &anchorId : m_model.anchorIds()) {
                present = present || m_model.anchor(anchorId)->name == panel.toMap().value(QStringLiteral("name")).toString();
            }
        }
        if (present) {
            description.insert(QStringLiteral("lastArrangement"), m_lastComposition);
        }
    }
    if (!currentLayoutName().isEmpty()) {
        description.insert(QStringLiteral("currentLayout"), currentLayoutName());
    }
    return description;
}

// Open views of files whose name, path or title is `reference`. Views in
// `libraryId` win over same-named files in other worktrees.
QStringList WorkspaceStore::openViewsNamed(const QString &reference, const QString &libraryId) const
{
    const QString wanted = QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/')).trimmed().toLower();
    QStringList here;
    QStringList elsewhere;
    for (const QString &viewId : m_model.viewIds()) {
        const ViewRecord *view = m_model.view(viewId);
        if (!view || (view->resource.kind != QStringLiteral("document") && view->resource.kind != QStringLiteral("file"))) {
            continue;
        }
        const QVariantMap &details = view->resource.details;
        const QString path = details.value(QStringLiteral("relativePath"), details.value(QStringLiteral("path"))).toString().toLower();
        const bool matches = view->id.toLower() == wanted || path == wanted || path.endsWith(QLatin1Char('/') + wanted)
            || details.value(QStringLiteral("name")).toString().toLower() == wanted || view->title.toLower() == wanted;
        if (!matches) {
            continue;
        }
        (libraryId.isEmpty() || view->resource.libraryId.isEmpty() || view->resource.libraryId == libraryId ? here : elsewhere).append(viewId);
    }
    return here.isEmpty() ? elsewhere : here;
}

// After a compose: the panels exist, hold what was put in them, are big
// enough to use, focus went where it was asked (or stayed), and nothing
// that was open is gone. Empty when all is well.
QString WorkspaceStore::verifyComposition(const WorkspaceModel &model, const QVariantMap &intent, qsizetype viewsBefore, bool focusMoved) const
{
    const QString surfaceId = primarySurfaceId();
    const LayoutGeometry geometry = model.computeGeometry(surfaceId, surfaceSize(surfaceId), m_metrics);
    for (const QVariant &value : intent.value(QStringLiteral("slots")).toList()) {
        const QVariantMap slot = value.toMap();
        const QString anchorId = slot.value(QStringLiteral("anchorId")).toString();
        const AnchorRecord *anchor = model.anchor(anchorId);
        if (!anchor) {
            return QStringLiteral("A panel of the new arrangement went missing.");
        }
        const QRectF rect = geometry.anchors.value(anchorId);
        // A little slack for splitters and rounding.
        if (rect.width() + 12 < Compose::kMinimumPanelWidth || rect.height() + 12 < Compose::kMinimumPanelHeight) {
            return QStringLiteral("“%1” would only get %2 x %3 pixels.").arg(model.anchorDisplayName(anchorId)).arg(qRound(rect.width())).arg(qRound(rect.height()));
        }
        for (const QVariant &itemValue : slot.value(QStringLiteral("items")).toList()) {
            const QVariantMap item = itemValue.toMap();
            QString viewId = item.value(QStringLiteral("viewId")).toString();
            if (viewId.isEmpty() && item.contains(QStringLiteral("resource"))) {
                const QVariantMap resource = item.value(QStringLiteral("resource")).toMap();
                const QStringList views = model.viewsForResource(resource.value(QStringLiteral("kind")).toString(), resource.value(QStringLiteral("id")).toString());
                for (const QString &candidate : views) {
                    if (anchor->views.contains(candidate)) {
                        viewId = candidate;
                    }
                }
                if (viewId.isEmpty() && !views.isEmpty()) {
                    viewId = views.first();
                }
                if (viewId.isEmpty()) {
                    return QStringLiteral("%1 didn't open.").arg(item.value(QStringLiteral("title")).toString());
                }
            }
            // A view a locked panel kept is reported, not an error.
            if (!viewId.isEmpty() && !model.view(viewId)) {
                return QStringLiteral("A view disappeared while arranging.");
            }
        }
    }
    const QString focus = intent.value(QStringLiteral("focusAnchor")).toString();
    if (!focus.isEmpty() && focusMoved && model.focusedAnchorId() != focus) {
        return QStringLiteral("Focus didn't move to the panel that was asked for.");
    }
    if (model.viewIds().size() < viewsBefore) {
        return QStringLiteral("Something that was open would have closed.");
    }
    return {};
}

QVariantList WorkspaceStore::panelsForAgent() const
{
    QVariantList panels;
    for (const QVariant &value : Intents::describe(m_model, agentEnvironment(false)).value(QStringLiteral("areas")).toList()) {
        const QVariantMap area = value.toMap();
        QStringList shows;
        for (const QVariant &entry : area.value(QStringLiteral("shows")).toList()) {
            shows.append(entry.toMap().value(QStringLiteral("title")).toString());
        }
        QVariantMap panel{
            {QStringLiteral("name"), area.value(QStringLiteral("area"))},
            {QStringLiteral("position"), area.value(QStringLiteral("position"))},
            {QStringLiteral("shows"), shows},
        };
        const QString anchorId = area.value(QStringLiteral("id")).toString();
        const QString surfaceId = m_model.surfaceOfAnchor(anchorId);
        const QSizeF size = surfaceSize(surfaceId);
        const QRectF rect = m_model.computeGeometry(surfaceId, size, m_metrics).anchors.value(anchorId);
        if (size.width() > 0 && size.height() > 0) {
            panel.insert(QStringLiteral("widthPercent"), qRound(rect.width() / size.width() * 100));
            panel.insert(QStringLiteral("heightPercent"), qRound(rect.height() / size.height() * 100));
        }
        panels.append(panel);
    }
    return panels;
}

// Tool vocabulary (files, chats, terminals, open views) -> intent items
// (view types and resources).
QVariantMap WorkspaceStore::agentIntent(const QVariantMap &request, QVariantMap *failed) const
{
    const QString action = request.value(QStringLiteral("action")).toString();
    QVariantMap intent = request;
    intent.remove(QStringLiteral("runId"));
    intent.remove(QStringLiteral("chatId"));
    intent.remove(QStringLiteral("explicit"));
    intent.remove(QStringLiteral("saveAs"));
    intent.remove(QStringLiteral("asked"));

    if (action == QStringLiteral("compose")) {
        return composeIntent(request, failed);
    }

    if (action != QStringLiteral("show")) {
        return intent;
    }

    const QString chatId = request.value(QStringLiteral("chatId")).toString();
    QVariantList items;

    for (const QVariant &value : request.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        const QString reference = item.value(QStringLiteral("reference")).toString().trimmed();

        // Already shaped as an intent item (typeId/resource, viewId, surface).
        if (kind.isEmpty()) {
            items.append(item);
            continue;
        }

        // Not (or not clearly) in the catalog: maybe it's open already.
        const QString lookup = item.value(QStringLiteral("lookup")).toString();
        if (kind == QStringLiteral("file") && !lookup.isEmpty() && lookup != QStringLiteral("found")) {
            const QStringList open = openViewsNamed(reference, item.value(QStringLiteral("libraryId")).toString());
            if (open.size() == 1) {
                items.append(QVariantMap{{QStringLiteral("viewId"), open.first()}});
                continue;
            }
            if (lookup == QStringLiteral("ambiguous") || open.size() > 1) {
                QStringList candidates = item.value(QStringLiteral("candidates")).toStringList();
                for (const QString &viewId : open) {
                    candidates.append(m_model.view(viewId)->resource.details.value(QStringLiteral("relativePath"), m_model.view(viewId)->title).toString());
                }
                *failed = failure(QStringLiteral("AMBIGUOUS"), QStringLiteral("“%1” could be %2.").arg(reference, candidates.join(QStringLiteral(" or "))));
                return {};
            }
            *failed = failure(QStringLiteral("NOT_FOUND"), QStringLiteral("“%1” isn't open or in the Library.").arg(reference));
            return {};
        }

        if (kind == QStringLiteral("file")) {
            const QVariantMap file = item.value(QStringLiteral("file")).toMap();
            const QString libraryId = item.value(QStringLiteral("libraryId")).toString();
            const ResourceRef resource = Resources::document(m_collectionId, libraryId, file);
            const Resources::FileView fileView = Resources::viewForFile(
                resource.details.value(QStringLiteral("name")).toString(),
                resource.details.value(QStringLiteral("extension")).toString(),
                resource.details.value(QStringLiteral("mimeType")).toString()
            );
            // An editor or a preview of the file is the same thing to show.
            QString typeId = fileView.typeId;
            for (const QString &viewId : m_model.viewsForResource(resource.kind, resource.id)) {
                if (const ViewRecord *view = m_model.view(viewId)) {
                    typeId = view->typeId;
                    break;
                }
            }
            items.append(QVariantMap{
                {QStringLiteral("typeId"), typeId},
                {QStringLiteral("resource"), resourceToMap(resource)},
                {QStringLiteral("title"), resource.details.value(QStringLiteral("name"))},
            });
            continue;
        }

        if (kind == QStringLiteral("chat")) {
            const QString wanted = reference.isEmpty() ? chatId : reference;
            QString docked;
            for (const QString &viewId : m_model.viewsForResource(QStringLiteral("chat"), wanted)) {
                if (m_model.view(viewId)->typeId == ViewTypes::Chat) {
                    docked = viewId;
                    break;
                }
            }
            const bool keepAtBottom = wanted == chatId && preferenceEffects().chatAtBottom;
            if (!docked.isEmpty() && !keepAtBottom) {
                items.append(QVariantMap{{QStringLiteral("viewId"), docked}});
            } else if (wanted == chatId && (keepAtBottom || !request.value(QStringLiteral("composing")).toBool())) {
                // This conversation lives in the bottom workbench (a
                // composed arrangement that names it gives it a panel).
                items.append(QVariantMap{{QStringLiteral("surface"), QStringLiteral("chat")}});
            } else {
                items.append(QVariantMap{
                    {QStringLiteral("typeId"), ViewTypes::Chat},
                    {QStringLiteral("resource"), resourceToMap(Resources::chat(wanted))},
                    {QStringLiteral("title"), QString()},
                });
            }
            continue;
        }

        if (kind == QStringLiteral("terminal")) {
            if (reference.compare(QStringLiteral("new"), Qt::CaseInsensitive) == 0) {
                const QString libraryId = item.value(QStringLiteral("libraryId")).toString();
                if (libraryId.isEmpty()) {
                    *failed = failure(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("A new terminal needs a Library to start in."));
                    return {};
                }
                const QString sessionId = QStringLiteral("terminal-") + QUuid::createUuid().toString(QUuid::Id128).left(10);
                items.append(QVariantMap{
                    {QStringLiteral("typeId"), ViewTypes::Terminal},
                    {QStringLiteral("resource"), resourceToMap(Resources::terminal(m_collectionId, libraryId, sessionId))},
                    {QStringLiteral("title"), QStringLiteral("Terminal")},
                });
                continue;
            }
            // The terminal already open in a panel, else the workbench's.
            QString existing;
            for (const QString &anchorId : m_recentAnchors + m_model.anchorIds()) {
                const AnchorRecord *anchor = m_model.anchor(anchorId);
                if (!anchor) {
                    continue;
                }
                for (const QString &viewId : anchor->views) {
                    const ViewRecord *view = m_model.view(viewId);
                    if (view && view->typeId == ViewTypes::Terminal
                        && (reference.isEmpty() || view->title.contains(reference, Qt::CaseInsensitive))) {
                        existing = viewId;
                        break;
                    }
                }
                if (!existing.isEmpty()) {
                    break;
                }
            }
            items.append(existing.isEmpty() ? QVariantMap{{QStringLiteral("surface"), QStringLiteral("terminal")}}
                                            : QVariantMap{{QStringLiteral("viewId"), existing}});
            continue;
        }

        if (kind == QStringLiteral("diff")) {
            const QStringList diffs = viewIdsOfType(ViewTypes::Diff);
            if (diffs.isEmpty()) {
                *failed = failure(
                    QStringLiteral("UNSUPPORTED"),
                    QStringLiteral("Diffs can't open in a panel yet. The Git section of the Explorer shows the changes.")
                );
                return {};
            }
            items.append(QVariantMap{{QStringLiteral("viewId"), diffs.first()}});
            continue;
        }

        if (kind == QStringLiteral("open_view")) {
            QString found = m_model.view(reference) ? reference : QString();
            for (int pass = 0; pass < 2 && found.isEmpty(); ++pass) {
                for (const QString &viewId : m_model.viewIds()) {
                    const ViewRecord *view = m_model.view(viewId);
                    const QString title = view->title.isEmpty() ? view->resource.details.value(QStringLiteral("name")).toString() : view->title;
                    const QString path = view->resource.details.value(QStringLiteral("path")).toString();
                    const bool match = pass == 0
                        ? title.compare(reference, Qt::CaseInsensitive) == 0 || path.compare(reference, Qt::CaseInsensitive) == 0
                        : reference.size() >= 3 && (title.contains(reference, Qt::CaseInsensitive) || path.contains(reference, Qt::CaseInsensitive));
                    if (match) {
                        found = viewId;
                        break;
                    }
                }
            }
            if (found.isEmpty()) {
                *failed = failure(QStringLiteral("NOT_FOUND"), QStringLiteral("Nothing open is called “%1”.").arg(reference));
                return {};
            }
            items.append(QVariantMap{{QStringLiteral("viewId"), found}});
            continue;
        }

        *failed = failure(QStringLiteral("UNSUPPORTED"), QStringLiteral("Archivist can't show a “%1” yet.").arg(kind));
        return {};
    }

    intent.insert(QStringLiteral("items"), items);
    return intent;
}

// workspace_compose: the description of a whole workspace becomes one
// Intents compose request. Items that can't be found are reported, not fatal.
QVariantMap WorkspaceStore::composeIntent(const QVariantMap &request, QVariantMap *failed) const
{
    const QString chatId = request.value(QStringLiteral("chatId")).toString();
    Compose::Spec spec;
    spec.templateId = request.value(QStringLiteral("template")).toString();
    spec.strategy = request.value(QStringLiteral("strategy")).toString();
    spec.focus = request.value(QStringLiteral("focus")).toString();
    spec.viewport = surfaceSize(primarySurfaceId());
    spec.rows = request.value(QStringLiteral("rows")).toInt();
    spec.columns = request.value(QStringLiteral("columns")).toInt();

    // What happened to each thing asked for: reused (already open), opened,
    // not_found, ambiguous or unsupported.
    QVariantList resources;
    for (const QString &problem : request.value(QStringLiteral("unresolved")).toStringList()) {
        resources.append(QVariantMap{{QStringLiteral("status"), QStringLiteral("not_found")}, {QStringLiteral("detail"), problem}});
    }

    QVariantList composition;
    for (const QVariant &value : request.value(QStringLiteral("slots")).toList()) {
        const QVariantMap given = value.toMap();
        Compose::Slot slot;
        slot.name = given.value(QStringLiteral("name")).toString();
        slot.holds = given.value(QStringLiteral("holds")).toStringList();
        slot.weight = given.value(QStringLiteral("weight")).toDouble();
        slot.position = given.value(QStringLiteral("position")).toString();
        QStringList references;
        for (const QVariant &value : given.value(QStringLiteral("items")).toList()) {
            const QVariantMap item = value.toMap();
            const QString kind = item.value(QStringLiteral("kind")).toString();
            QString reference = item.value(QStringLiteral("reference")).toString();
            if (reference.isEmpty()) {
                reference = item.value(QStringLiteral("file")).toMap().value(QStringLiteral("relativePath")).toString();
            }
            if (reference.isEmpty()) {
                reference = kind;
            }
            references.append(kind == QStringLiteral("file") ? reference : kind + QLatin1Char(':') + reference);
            QVariantMap entry{{QStringLiteral("reference"), reference}, {QStringLiteral("panel"), slot.name}};

            QVariantMap problem;
            const QVariantMap shaped = agentIntent({
                {QStringLiteral("action"), QStringLiteral("show")},
                {QStringLiteral("composing"), true},
                {QStringLiteral("chatId"), chatId},
                {QStringLiteral("items"), QVariantList{item}},
            }, &problem);
            if (!problem.isEmpty()) {
                const QString code = problem.value(QStringLiteral("code")).toString();
                entry.insert(QStringLiteral("status"), code == QStringLiteral("AMBIGUOUS") ? QStringLiteral("ambiguous")
                                                    : code == QStringLiteral("UNSUPPORTED") ? QStringLiteral("unsupported")
                                                                                            : QStringLiteral("not_found"));
                entry.insert(QStringLiteral("detail"), problem.value(QStringLiteral("error")));
                resources.append(entry);
                continue;
            }
            for (const QVariant &shapedItem : shaped.value(QStringLiteral("items")).toList()) {
                const QVariantMap map = shapedItem.toMap();
                QString status = QStringLiteral("opened");
                if (!map.value(QStringLiteral("viewId")).toString().isEmpty()) {
                    status = QStringLiteral("reused");
                } else if (!map.value(QStringLiteral("surface")).toString().isEmpty()) {
                    status = QStringLiteral("bottom_panel");
                } else {
                    const QVariantMap resource = map.value(QStringLiteral("resource")).toMap();
                    if (!m_model.viewsForResource(resource.value(QStringLiteral("kind")).toString(), resource.value(QStringLiteral("id")).toString()).isEmpty()) {
                        status = QStringLiteral("reused");
                    }
                }
                entry.insert(QStringLiteral("status"), status);
                slot.items.append(map);
            }
            resources.append(entry);
        }
        spec.panels.append(slot);
        composition.append(QVariantMap{
            {QStringLiteral("name"), slot.name},
            {QStringLiteral("holds"), slot.holds},
            {QStringLiteral("weight"), slot.weight},
            {QStringLiteral("position"), slot.position},
            {QStringLiteral("items"), references},
        });
    }

    // Replace builds a new arrangement from a recipe; patch keeps the panels
    // that exist and only changes the ones named. A recipe, template or grid
    // size always means replace.
    const bool recipeNamed = !spec.templateId.isEmpty() || !spec.strategy.isEmpty() || spec.rows > 0 || spec.columns > 0;
    const QString mode = recipeNamed || request.value(QStringLiteral("mode")).toString() == QStringLiteral("replace")
        ? QStringLiteral("replace")
        : QStringLiteral("patch");
    if (mode == QStringLiteral("patch")) {
        QVariantMap intent = patchIntent(spec, failed);
        if (!failed->isEmpty()) {
            return {};
        }
        intent.insert(QStringLiteral("resources"), resources);
        intent.insert(QStringLiteral("composition"), QVariantMap{
            {QStringLiteral("mode"), mode},
            {QStringLiteral("panels"), composition},
        });
        return intent;
    }

    const Compose::Plan plan = Compose::plan(spec, m_model);
    if (!plan.ok) {
        *failed = failure(plan.code, plan.message);
        return {};
    }
    QVariantMap intent = plan.request;
    intent.insert(QStringLiteral("resources"), resources);
    intent.insert(QStringLiteral("adjustments"), plan.adjustments);
    intent.insert(QStringLiteral("composition"), QVariantMap{
        {QStringLiteral("mode"), mode},
        {QStringLiteral("template"), spec.templateId},
        {QStringLiteral("strategy"), spec.strategy},
        {QStringLiteral("rows"), spec.rows},
        {QStringLiteral("columns"), spec.columns},
        {QStringLiteral("panels"), composition},
    });
    return intent;
}

// A patch as ordinary arrange steps, run as one change: items go to the
// panel of the same name, a new name adds one panel at its side, and
// panels nobody named stay exactly as they are.
QVariantMap WorkspaceStore::patchIntent(const Compose::Spec &spec, QVariantMap *failed) const
{
    const auto existingPanel = [this](const QString &name) -> QString {
        if (name.trimmed().isEmpty()) {
            return {};
        }
        for (const QString &anchorId : m_model.anchorIds()) {
            if (m_model.surfaceOfAnchor(anchorId) == primarySurfaceId()
                && m_model.anchorDisplayName(anchorId).compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
                return anchorId;
            }
        }
        return {};
    };
    const auto sideOf = [](const QString &position) {
        const QString text = position.toLower();
        for (const QString side : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")}) {
            if (text.contains(side)) {
                return side;
            }
        }
        return QStringLiteral("right");
    };

    QVariantList steps;
    QString focusTarget;
    for (const Compose::Slot &slot : spec.panels) {
        QString target = existingPanel(slot.name);
        if (target.isEmpty()) {
            const bool anyClosed = std::any_of(slot.items.cbegin(), slot.items.cend(), [](const QVariant &item) {
                return item.toMap().value(QStringLiteral("viewId")).toString().isEmpty();
            });
            // Something already open with no new place asked for stays put.
            if (slot.position.isEmpty() && !anyClosed && !slot.items.isEmpty()) {
                steps.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("show")}, {QStringLiteral("items"), slot.items}});
                continue;
            }
            steps.append(QVariantMap{
                {QStringLiteral("action"), QStringLiteral("add")},
                {QStringLiteral("target"), QString()},
                {QStringLiteral("to"), sideOf(slot.position)},
                {QStringLiteral("name"), slot.name},
            });
            target = slot.name;
        }
        if (!slot.items.isEmpty()) {
            steps.append(QVariantMap{
                {QStringLiteral("action"), QStringLiteral("show")},
                {QStringLiteral("items"), slot.items},
                {QStringLiteral("where"), target},
            });
        }
        if (!spec.focus.isEmpty() && slot.name.compare(spec.focus, Qt::CaseInsensitive) == 0) {
            focusTarget = target;
        }
    }
    if (steps.isEmpty()) {
        *failed = failure(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("Nothing to change: name a panel and what goes in it."));
        return {};
    }
    if (!focusTarget.isEmpty()) {
        steps.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("focus")}, {QStringLiteral("target"), focusTarget}});
    }
    QVariantMap intent{{QStringLiteral("action"), QStringLiteral("steps")}, {QStringLiteral("steps"), steps}};
    if (!focusTarget.isEmpty()) {
        intent.insert(QStringLiteral("focusAnchor"), focusTarget);
    }
    return intent;
}

void WorkspaceStore::logAgent(const QString &summary, const QString &status, const QString &chatId)
{
    m_agentLog.prepend(QVariantMap{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("status"), status},
        {QStringLiteral("chatId"), chatId},
    });
    while (m_agentLog.size() > kMaximumAgentLog) {
        m_agentLog.removeLast();
    }
}

QVariantMap WorkspaceStore::applyAgentIntent(const QVariantMap &intent, const QString &chatId, bool mayMoveFocus)
{
    if (m_drag.kind != DragSource::Kind::None) {
        cancelDrag();
    }

    HistoryStep step = historyStep(QString());
    const Intents::Outcome outcome = Intents::perform(m_model, intent, agentEnvironment(mayMoveFocus));

    if (!outcome.ok) {
        logAgent(outcome.message, QStringLiteral("refused"), chatId);
        emit agentChanged();
        return failure(outcome.code, outcome.message);
    }

    const bool surfaceChanges = !outcome.commandSurface.isEmpty()
        && (m_commandSurfaceCollapsed || m_commandSurfaceView != outcome.commandSurface);
    const QString summary = summaryOf(outcome.notes);

    if (outcome.changed || surfaceChanges) {
        step.close = outcome.opened;
        step.summary = summary;
        step.surfaceTouched = surfaceChanges;
        rememberArrangement(step.state, outcome.opened, summary);
        recordStep(step);
    }

    if (outcome.changed) {
        publish();
        for (const QString &viewId : outcome.opened) {
            emit viewOpened(viewId);
        }
        if (!outcome.focusAnchorId.isEmpty() && !focusedViewId().isEmpty()) {
            emit viewFocused(focusedViewId());
        }
    }

    if (!outcome.commandSurface.isEmpty()) {
        emit commandSurfaceRequested(outcome.commandSurface, true);
    }

    logAgent(summary, QStringLiteral("applied"), chatId);
    emit agentChanged();
    if (outcome.changed || surfaceChanges) {
        emit agentArranged(summary);
    }

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("status"), outcome.changed || surfaceChanges ? QStringLiteral("applied") : QStringLiteral("unchanged")},
        {QStringLiteral("notes"), outcome.notes},
    };
}

QVariantMap WorkspaceStore::suggestAgentIntent(const QVariantMap &intent, const QString &chatId, const QString &reason)
{
    // Check it now, against a copy, so the agent hears about problems.
    WorkspaceModel trial = m_model;
    const Intents::Outcome outcome = Intents::perform(trial, intent, agentEnvironment(false));

    if (!outcome.ok) {
        return failure(outcome.code, outcome.message);
    }

    if (!outcome.changed && outcome.commandSurface.isEmpty()) {
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("status"), QStringLiteral("unchanged")},
            {QStringLiteral("notes"), outcome.notes},
        };
    }

    const QString summary = summaryOf(outcome.notes);
    m_suggestion = {
        {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("notes"), outcome.notes},
        {QStringLiteral("chatId"), chatId},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    };
    m_suggestionIntent = intent;
    logAgent(summary, QStringLiteral("suggested"), chatId);
    emit agentChanged();

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("status"), QStringLiteral("suggested")},
        {QStringLiteral("notes"), outcome.notes},
        {QStringLiteral("message"), QStringLiteral("Prepared, not applied: %1. The person can apply it from the workspace notice.").arg(reason)},
    };
}

QVariantMap WorkspaceStore::runAgentRequest(const QVariantMap &request, bool foreground)
{
    const QString action = request.value(QStringLiteral("action")).toString();
    const QString chatId = request.value(QStringLiteral("chatId")).toString();

    if (action == QStringLiteral("describe")) {
        QVariantMap description = describeForAgent();
        description.insert(QStringLiteral("ok"), true);
        return description;
    }

    // Only a person's explicit request in the conversation in front changes
    // the workspace right away.
    QString holdBack;
    if (!request.value(QStringLiteral("explicit")).toBool()) {
        holdBack = QStringLiteral("the person didn't ask for a workspace change");
    } else if (!foreground) {
        holdBack = QStringLiteral("this conversation isn't the one in front");
    } else if (m_drag.kind != DragSource::Kind::None || m_resizing) {
        holdBack = QStringLiteral("the person is moving a panel");
    }

    if (action == QStringLiteral("layout")) {
        const QString op = request.value(QStringLiteral("op")).toString();
        if (op == QStringLiteral("apply")) {
            if (!holdBack.isEmpty()) {
                return failure(QStringLiteral("NOT_ALLOWED"), QStringLiteral("Not switching layouts now: %1.").arg(holdBack));
            }
            QVariantMap restore = request;
            restore.insert(QStringLiteral("scope"), QStringLiteral("saved_layout"));
            restore.insert(QStringLiteral("layout"), request.value(QStringLiteral("name")));
            return restoreForAgent(restore);
        }
        if (op != QStringLiteral("list") && !holdBack.isEmpty()) {
            return failure(QStringLiteral("NOT_ALLOWED"), QStringLiteral("Not changing saved layouts now: %1.").arg(holdBack));
        }
        return layoutForAgent(request);
    }

    if (action == QStringLiteral("preference")) {
        if (request.value(QStringLiteral("op")).toString() != QStringLiteral("list") && !holdBack.isEmpty()) {
            return failure(QStringLiteral("NOT_ALLOWED"), QStringLiteral("Not changing preferences now: %1.").arg(holdBack));
        }
        return preferenceForAgent(request);
    }

    // "Don't rearrange my windows unless I ask": only explicit requests.
    const bool asked = request.contains(QStringLiteral("asked")) ? request.value(QStringLiteral("asked")).toBool() : request.value(QStringLiteral("explicit")).toBool();
    if (holdBack.isEmpty() && preferenceEffects().onlyWhenAsked && !asked) {
        holdBack = QStringLiteral("the person asked not to rearrange unless they ask");
    }

    if (action == QStringLiteral("restore")) {
        if (!holdBack.isEmpty()) {
            return failure(QStringLiteral("NOT_ALLOWED"), QStringLiteral("Not restoring now: %1.").arg(holdBack));
        }
        return restoreForAgent(request);
    }

    QVariantMap failed;
    const QVariantMap intent = agentIntent(request, &failed);
    if (!failed.isEmpty()) {
        logAgent(failed.value(QStringLiteral("error")).toString(), QStringLiteral("refused"), chatId);
        return failed;
    }

    const QString saveAs = request.value(QStringLiteral("saveAs")).toString().trimmed();

    if (!holdBack.isEmpty()) {
        QVariantMap suggested = suggestAgentIntent(intent, chatId, holdBack);
        if (action == QStringLiteral("compose")) {
            suggested.insert(QStringLiteral("resources"), intent.value(QStringLiteral("resources")));
        }
        if (!saveAs.isEmpty() && suggested.value(QStringLiteral("ok")).toBool()) {
            suggested.insert(QStringLiteral("layout"), QStringLiteral("Not saved as “%1”: the change wasn't applied.").arg(saveAs));
        }
        return suggested;
    }

    const bool focusWanted = request.value(QStringLiteral("focus")).toBool() || action == QStringLiteral("focus")
        || (action == QStringLiteral("compose") && !intent.value(QStringLiteral("focusAnchor")).toString().isEmpty());
    const qsizetype viewsBefore = m_model.viewIds().size();
    const bool mayMoveFocus = focusWanted && !userBusy();
    if (action == QStringLiteral("compose")) {
        // Tried on a copy and checked first, so nothing is shown or
        // reported as applied unless it holds.
        WorkspaceModel trial = m_model;
        const Intents::Outcome outcome = Intents::perform(trial, intent, agentEnvironment(mayMoveFocus));
        if (!outcome.ok) {
            logAgent(outcome.message, QStringLiteral("refused"), chatId);
            return failure(outcome.code, outcome.message);
        }
        const QString problem = verifyComposition(trial, intent, viewsBefore, mayMoveFocus);
        if (!problem.isEmpty()) {
            logAgent(problem, QStringLiteral("refused"), chatId);
            return failure(QStringLiteral("VERIFY_FAILED"), QStringLiteral("%1 Nothing was changed.").arg(problem));
        }
    }
    QVariantMap result = applyAgentIntent(intent, chatId, mayMoveFocus);
    if (action == QStringLiteral("compose") && result.value(QStringLiteral("ok")).toBool()) {
        m_lastComposition = intent.value(QStringLiteral("composition")).toMap();
        // This chat now has a panel of its own: the same chat in the bottom
        // panel would only be a duplicate, so it folds away (undo brings
        // it back).
        bool chatInPanel = false;
        for (const QString &viewId : m_model.viewsForResource(QStringLiteral("chat"), chatId)) {
            const ViewRecord *view = m_model.view(viewId);
            chatInPanel = chatInPanel || (view && view->typeId == ViewTypes::Chat && m_model.surfaceOfAnchor(m_model.anchorOfView(viewId)) == primarySurfaceId());
        }
        if (chatInPanel && !chatId.isEmpty() && !m_commandSurfaceCollapsed && m_commandSurfaceView == QStringLiteral("chat")
            && result.value(QStringLiteral("status")).toString() == QStringLiteral("applied") && !m_history.isEmpty()) {
            // The step already holds the bottom panel as it was (open, chat).
            m_history.last().surfaceTouched = true;
            m_commandSurfaceCollapsed = true;
            emit commandSurfaceRequested(QStringLiteral("chat"), false);
            emit commandSurfaceChanged();
            result.insert(QStringLiteral("bottomPanel"), QStringLiteral("The bottom chat is hidden while the chat has a panel."));
        }
        result.remove(QStringLiteral("notes"));
        result.insert(QStringLiteral("panels"), panelsForAgent());
        result.insert(QStringLiteral("resources"), intent.value(QStringLiteral("resources")));
        if (!intent.value(QStringLiteral("adjustments")).toStringList().isEmpty()) {
            result.insert(QStringLiteral("adjusted"), intent.value(QStringLiteral("adjustments")));
        }
    }

    // "…and save it as Review": arranging and saving in one request.
    if (!saveAs.isEmpty() && result.value(QStringLiteral("ok")).toBool()) {
        const QVariantMap saved = layoutForAgent({
            {QStringLiteral("op"), QStringLiteral("save")},
            {QStringLiteral("name"), saveAs},
            {QStringLiteral("replace"), false},
            {QStringLiteral("chatId"), chatId},
        });
        result.insert(
            QStringLiteral("layout"),
            saved.value(QStringLiteral("ok")).toBool()
                ? QVariant(QStringLiteral("Saved as the layout “%1”.").arg(saveAs))
                : QVariant(QStringLiteral("The arrangement is applied but wasn't saved: %1").arg(saved.value(QStringLiteral("error")).toString()))
        );
        if (saved.value(QStringLiteral("ok")).toBool()) {
            result.insert(QStringLiteral("currentLayout"), saveAs);
        }
    }
    return result;
}

bool WorkspaceStore::applyAgentSuggestion()
{
    if (m_suggestionIntent.isEmpty()) {
        return false;
    }
    const QVariantMap intent = m_suggestionIntent;
    const QString chatId = m_suggestion.value(QStringLiteral("chatId")).toString();
    m_suggestion.clear();
    m_suggestionIntent.clear();
    const QVariantMap result = applyAgentIntent(intent, chatId, false);
    if (!result.value(QStringLiteral("ok")).toBool()) {
        emit operationFailed(result.value(QStringLiteral("error")).toString());
        return false;
    }
    return true;
}

void WorkspaceStore::dismissAgentSuggestion()
{
    if (m_suggestion.isEmpty()) {
        return;
    }
    logAgent(m_suggestion.value(QStringLiteral("summary")).toString(), QStringLiteral("dismissed"), m_suggestion.value(QStringLiteral("chatId")).toString());
    m_suggestion.clear();
    m_suggestionIntent.clear();
    emit agentChanged();
}

QSet<QString> WorkspaceStore::closableAgentViews(const QStringList &opened) const
{
    QSet<QString> closable;
    for (const QString &viewId : opened) {
        const ViewRecord *view = m_model.view(viewId);
        // Terminals keep running; unsaved text is never closed.
        if (!view || view->typeId == ViewTypes::Terminal || !unsavedDocumentsForViews({viewId}).isEmpty()) {
            continue;
        }
        closable.insert(viewId);
    }
    return closable;
}

bool WorkspaceStore::restoreTo(const WorkspaceModel::Data &arrangement, const QStringList &opened, QString *code, QString *error,
                               const QStringList &reopen, QStringList *closed, QStringList *reopened)
{
    QHash<QString, QSizeF> sizes;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        sizes.insert(surfaceId, surfaceSize(surfaceId));
    }

    QList<ViewRecord> closing;
    const QSet<QString> closable = closableAgentViews(opened);
    for (const QString &viewId : closable) {
        closing.append(*m_model.view(viewId));
    }

    const Arrangements::Result result = Arrangements::restore(
        m_model, arrangement, closable, sizes, m_metrics, QSet<QString>(reopen.cbegin(), reopen.cend()));
    if (!result.ok) {
        *code = result.code;
        *error = result.message;
        return false;
    }

    publish();
    for (const ViewRecord &view : std::as_const(closing)) {
        emit viewClosed(view.id, view.typeId, resourceToMap(view.resource));
    }
    for (const QString &viewId : result.reopenedViews) {
        emit viewOpened(viewId);
    }
    if (closed) {
        *closed = result.closedViews;
    }
    if (reopened) {
        *reopened = result.reopenedViews;
    }
    return true;
}

// ---- history ---------------------------------------------------------------

WorkspaceStore::HistoryStep WorkspaceStore::historyStep(const QString &summary) const
{
    HistoryStep step;
    step.state = m_model.data();
    step.summary = summary;
    step.surfaceCollapsed = m_commandSurfaceCollapsed;
    step.surfaceView = m_commandSurfaceView;
    step.layoutId = m_currentLayoutId;
    step.arrangement = arrangementRecord();
    step.composer = m_composer.toVariant();
    return step;
}

void WorkspaceStore::recordStep(HistoryStep step)
{
    m_history.append(std::move(step));
    while (m_history.size() > kMaximumHistorySteps) {
        m_history.removeFirst();
    }
    m_redo.clear();
    emit historyChanged();
}

bool WorkspaceStore::restoreStep(const HistoryStep &step, HistoryStep *inverse, QString *code, QString *error)
{
    HistoryStep back = historyStep(step.summary);
    back.surfaceTouched = step.surfaceTouched;
    back.composerTouched = step.composerTouched;
    QStringList closed;
    QStringList reopened;
    if (!restoreTo(step.state, step.close, code, error, step.reopen, &closed, &reopened)) {
        return false;
    }
    back.close = reopened;
    back.reopen = closed;

    // The shell may not have reported the bottom panel's state since, so
    // ask for it either way.
    if (step.surfaceTouched) {
        setCommandSurfaceState(step.surfaceCollapsed, step.surfaceView);
        emit commandSurfaceRequested(step.surfaceView, !step.surfaceCollapsed);
    }
    if (step.layoutId != m_currentLayoutId) {
        setCurrentLayoutId(step.layoutId);
    }
    setArrangementRecord(step.arrangement);
    if (step.composerTouched) {
        Composer::State composer = Composer::State::fromVariant(step.composer);
        Composer::normalize(composer, m_composerModes, composerEnvironment());
        setComposerState(composer);
    }
    *inverse = back;
    return true;
}

bool WorkspaceStore::undoSteps(qsizetype count, QString *code, QString *error)
{
    if (m_history.isEmpty() || count <= 0) {
        *code = QStringLiteral("NOTHING_TO_RESTORE");
        *error = QStringLiteral("There's no workspace change to undo.");
        return false;
    }

    count = std::min(count, m_history.size());
    const qsizetype first = m_history.size() - count;
    HistoryStep target = m_history.at(first);
    for (qsizetype index = first + 1; index < m_history.size(); ++index) {
        const HistoryStep &later = m_history.at(index);
        target.close.append(later.close);
        target.reopen.append(later.reopen);
        target.surfaceTouched = target.surfaceTouched || later.surfaceTouched;
        target.composerTouched = target.composerTouched || later.composerTouched;
    }
    if (count > 1) {
        target.summary = QStringLiteral("%1 workspace changes").arg(count);
    }

    HistoryStep inverse;
    if (!restoreStep(target, &inverse, code, error)) {
        return false;
    }
    m_history.remove(first, count);
    m_redo.append(inverse);
    // A sidebar tab deleted since then can't take its panel back.
    adoptOrphanSidebarSurfaces();
    emit historyChanged();
    emit agentChanged();
    return true;
}

bool WorkspaceStore::undo()
{
    const QString summary = undoSummary();
    QString code;
    QString error;
    if (!undoSteps(1, &code, &error)) {
        emit operationFailed(error);
        return false;
    }
    logAgent(QStringLiteral("Undid: %1").arg(summary), QStringLiteral("restored"), {});
    return true;
}

bool WorkspaceStore::redoStep(QString *code, QString *error)
{
    if (m_redo.isEmpty()) {
        *code = QStringLiteral("NOTHING_TO_RESTORE");
        *error = QStringLiteral("There's nothing to redo.");
        return false;
    }
    HistoryStep inverse;
    if (!restoreStep(m_redo.last(), &inverse, code, error)) {
        return false;
    }
    m_redo.removeLast();
    m_history.append(inverse);
    adoptOrphanSidebarSurfaces();
    while (m_history.size() > kMaximumHistorySteps) {
        m_history.removeFirst();
    }
    emit historyChanged();
    emit agentChanged();
    return true;
}

bool WorkspaceStore::redo()
{
    const QString summary = redoSummary();
    QString code;
    QString error;
    if (!redoStep(&code, &error)) {
        emit operationFailed(error);
        return false;
    }
    logAgent(QStringLiteral("Redid: %1").arg(summary), QStringLiteral("applied"), {});
    return true;
}

// ---- temporary arrangements ---------------------------------------------

QString WorkspaceStore::arrangementKey(const QString &collectionId) const
{
    return QStringLiteral("workspace/collections/%1/docking/temporaryArrangement").arg(collectionId);
}

QVariantMap WorkspaceStore::temporaryArrangement() const
{
    if (m_baseline.isEmpty()) {
        return {};
    }
    return {
        {QStringLiteral("since"), m_baselineSince},
        {QStringLiteral("changes"), m_baselineChanges},
        {QStringLiteral("summary"), m_baselineChanges.isEmpty() ? QString() : m_baselineChanges.last()},
    };
}

void WorkspaceStore::rememberArrangement(const WorkspaceModel::Data &before, const QStringList &opened, const QString &summary)
{
    if (m_collectionId.isEmpty()) {
        return;
    }
    if (m_baseline.isEmpty()) {
        WorkspaceModel then = m_model;
        if (!then.setData(before).isEmpty()) {
            return;
        }
        QJsonObject snapshot = Codec::encodeSnapshot(then);
        // Only the arrangement: where focus was is not part of it.
        QJsonObject session = snapshot.value(QStringLiteral("session")).toObject();
        session.remove(QStringLiteral("focusedAnchor"));
        snapshot.insert(QStringLiteral("session"), session);
        m_baseline = snapshot;
        m_baselineSince = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_baselineOpened.clear();
        m_baselineChanges.clear();
    }
    for (const QString &viewId : opened) {
        if (!m_baselineOpened.contains(viewId)) {
            m_baselineOpened.append(viewId);
        }
    }
    if (!summary.isEmpty()) {
        m_baselineChanges.append(summary);
        while (m_baselineChanges.size() > kMaximumHistorySteps) {
            m_baselineChanges.removeFirst();
        }
    }

    const QJsonObject record = arrangementRecord();
    m_settings.setValue(arrangementKey(m_collectionId), QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
}

QJsonObject WorkspaceStore::arrangementRecord() const
{
    if (m_baseline.isEmpty()) {
        return {};
    }
    return {
        {QStringLiteral("baseline"), m_baseline},
        {QStringLiteral("opened"), QJsonArray::fromStringList(m_baselineOpened)},
        {QStringLiteral("changes"), QJsonArray::fromStringList(m_baselineChanges)},
        {QStringLiteral("since"), m_baselineSince},
    };
}

void WorkspaceStore::setArrangementRecord(const QJsonObject &record)
{
    if (record.value(QStringLiteral("baseline")).toObject().isEmpty()) {
        if (!m_baseline.isEmpty()) {
            forgetArrangement();
        }
        return;
    }
    m_baseline = record.value(QStringLiteral("baseline")).toObject();
    m_baselineOpened.clear();
    m_baselineChanges.clear();
    for (const QJsonValue &value : record.value(QStringLiteral("opened")).toArray()) {
        m_baselineOpened.append(value.toString());
    }
    for (const QJsonValue &value : record.value(QStringLiteral("changes")).toArray()) {
        m_baselineChanges.append(value.toString());
    }
    m_baselineSince = record.value(QStringLiteral("since")).toString();
    if (!m_collectionId.isEmpty()) {
        m_settings.setValue(arrangementKey(m_collectionId), QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
    }
}

void WorkspaceStore::loadArrangement()
{
    m_baseline = {};
    m_baselineOpened.clear();
    m_baselineChanges.clear();
    m_baselineSince.clear();
    if (m_collectionId.isEmpty()) {
        return;
    }
    const QJsonObject record = QJsonDocument::fromJson(m_settings.value(arrangementKey(m_collectionId)).toString().toUtf8()).object();
    const QJsonObject baseline = record.value(QStringLiteral("baseline")).toObject();
    if (baseline.isEmpty()) {
        return;
    }
    m_baseline = baseline;
    for (const QJsonValue &value : record.value(QStringLiteral("opened")).toArray()) {
        m_baselineOpened.append(value.toString());
    }
    for (const QJsonValue &value : record.value(QStringLiteral("changes")).toArray()) {
        m_baselineChanges.append(value.toString());
    }
    m_baselineSince = record.value(QStringLiteral("since")).toString();
}

void WorkspaceStore::forgetArrangement()
{
    m_baseline = {};
    m_baselineOpened.clear();
    m_baselineChanges.clear();
    m_baselineSince.clear();
    if (!m_collectionId.isEmpty()) {
        m_settings.remove(arrangementKey(m_collectionId));
    }
}

bool WorkspaceStore::restoreArrangement(QString *code, QString *error)
{
    if (m_baseline.isEmpty()) {
        // Without a collection nothing is remembered: undo everything instead.
        if (m_collectionId.isEmpty() && !m_history.isEmpty()) {
            return undoSteps(m_history.size(), code, error);
        }
        *code = QStringLiteral("NOTHING_TO_RESTORE");
        *error = QStringLiteral("There's no temporary arrangement to put back.");
        return false;
    }

    WorkspaceModel then;
    const Codec::LoadReport report = Codec::decodeSnapshot(m_baseline, m_registry, then);
    if (!report.ok) {
        forgetArrangement();
        emit agentChanged();
        *code = QStringLiteral("INVALID_ARRANGEMENT");
        *error = QStringLiteral("The earlier arrangement can't be read anymore.");
        return false;
    }

    // Putting back is a step too: undo returns to the arrangement it replaced.
    HistoryStep step = historyStep(QStringLiteral("Put the workspace back the way it was"));
    // Only this task's steps: the last one that found no temporary
    // arrangement started it.
    qsizetype taskStart = 0;
    for (qsizetype index = m_history.size() - 1; index >= 0; --index) {
        if (m_history.at(index).arrangement.isEmpty()) {
            taskStart = index;
            break;
        }
    }
    QStringList opened = m_baselineOpened;
    const HistoryStep *surfaceBefore = nullptr; // the bottom panel as the task found it
    for (qsizetype index = taskStart; index < m_history.size(); ++index) {
        const HistoryStep &earlier = m_history.at(index);
        opened.append(earlier.close);
        if (!surfaceBefore && earlier.surfaceTouched) {
            surfaceBefore = &earlier;
        }
    }
    const bool surfaceTouched = surfaceBefore != nullptr;
    const bool collapse = surfaceTouched && surfaceBefore->surfaceCollapsed;
    const QString view = surfaceTouched ? surfaceBefore->surfaceView : QString();

    QStringList closed;
    if (!restoreTo(then.data(), opened, code, error, {}, &closed)) {
        return false;
    }
    step.reopen = closed;
    if (surfaceTouched) {
        step.surfaceTouched = true;
        setCommandSurfaceState(collapse, view);
        emit commandSurfaceRequested(view, !collapse);
    }
    forgetArrangement();
    recordStep(step);
    emit agentChanged();
    return true;
}

bool WorkspaceStore::putBackArrangement()
{
    QString code;
    QString error;
    if (!restoreArrangement(&code, &error)) {
        emit operationFailed(error);
        return false;
    }
    logAgent(QStringLiteral("Put the workspace back the way it was"), QStringLiteral("restored"), {});
    emit agentRestored(QStringLiteral("Put the workspace back the way it was"));
    return true;
}

void WorkspaceStore::keepArrangement()
{
    if (m_baseline.isEmpty()) {
        return;
    }
    // The arrangement becomes the normal one; the history stays.
    forgetArrangement();
    logAgent(QStringLiteral("Kept the current arrangement"), QStringLiteral("kept"), {});
    emit agentChanged();
}

bool WorkspaceStore::undoAgentChange()
{
    return undo();
}

QVariantMap WorkspaceStore::restoreForAgent(const QVariantMap &request)
{
    const QString scope = request.value(QStringLiteral("scope")).toString();
    const QString chatId = request.value(QStringLiteral("chatId")).toString();

    if (scope == QStringLiteral("saved_layout")) {
        const QString name = request.value(QStringLiteral("layout")).toString().trimmed();
        const QString layoutId = layoutIdNamed(name);
        if (layoutId.isEmpty()) {
            return failure(QStringLiteral("NOT_FOUND"), QStringLiteral("There's no saved layout called “%1”.").arg(name));
        }
        const QJsonObject layout = layoutDocument(layoutId);
        const WorkspaceModel::Data before = m_model.data();
        WorkspaceModel trial = m_model;
        const Codec::LoadReport report = Codec::applyLayout(layout, trial);
        if (!report.ok) {
            return failure(QStringLiteral("LAYOUT_LOCKED"), report.error);
        }
        if (!applyLayout(layoutId) || currentLayoutId() != layoutId) {
            return failure(QStringLiteral("FAILED"), QStringLiteral("The layout couldn't be applied."));
        }
        // applyLayout recorded the history step.
        const QString summary = QStringLiteral("Switched to the “%1” layout").arg(currentLayoutName());
        rememberArrangement(before, {}, summary);
        logAgent(summary, QStringLiteral("applied"), chatId);
        emit agentChanged();
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("notes"), QStringList{summary}},
            {QStringLiteral("currentLayout"), currentLayoutName()},
            {QStringLiteral("panels"), panelsForAgent()},
        };
    }

    QString code;
    QString error;
    if (scope == QStringLiteral("redo")) {
        const QString summary = QStringLiteral("Redid: %1").arg(redoSummary());
        if (!redoStep(&code, &error)) {
            return failure(code, error);
        }
        logAgent(summary, QStringLiteral("applied"), chatId);
        emit agentRestored(summary);
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("notes"), QStringList{summary}},
        };
    }
    const bool restored = scope == QStringLiteral("before_task") || m_history.isEmpty()
        ? restoreArrangement(&code, &error)
        : undoSteps(1, &code, &error);
    if (!restored) {
        return failure(code, error);
    }

    const QString summary = scope == QStringLiteral("before_task")
        ? QStringLiteral("Put the workspace back the way it was")
        : QStringLiteral("Undid the last workspace change");
    logAgent(summary, QStringLiteral("restored"), chatId);
    emit agentRestored(summary);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("status"), QStringLiteral("applied")},
        {QStringLiteral("notes"), QStringList{summary}},
    };
}

// ---- preferences ----------------------------------------------------------

namespace
{
const QString kPreferencesKey = QStringLiteral("workspace/preferences");
}

void WorkspaceStore::loadPreferences()
{
    const QJsonArray stored = QJsonDocument::fromJson(m_settings.value(kPreferencesKey).toString().toUtf8()).array();
    QList<Preferences::Preference> session;
    for (const Preferences::Preference &preference : std::as_const(m_preferences)) {
        if (!preference.lasting) {
            session.append(preference);
        }
    }
    m_preferences = Preferences::fromJson(stored) + session;
}

void WorkspaceStore::savePreferences()
{
    QList<Preferences::Preference> lasting;
    for (const Preferences::Preference &preference : std::as_const(m_preferences)) {
        if (preference.lasting) {
            lasting.append(preference);
        }
    }
    if (lasting.isEmpty()) {
        m_settings.remove(kPreferencesKey);
    } else {
        m_settings.setValue(kPreferencesKey, QString::fromUtf8(QJsonDocument(Preferences::toJson(lasting)).toJson(QJsonDocument::Indented)));
    }
}

Preferences::Effects WorkspaceStore::preferenceEffects() const
{
    return Preferences::effects(m_preferences, m_collectionId);
}

QVariantList WorkspaceStore::workspacePreferences() const
{
    QVariantList list;
    for (const Preferences::Preference &preference : m_preferences) {
        if (!preference.collectionId.isEmpty() && preference.collectionId != m_collectionId) {
            continue;
        }
        QVariantMap map = Preferences::toMap(preference);
        const QString area = preference.anchorId.isEmpty() || !m_model.anchor(preference.anchorId) ? QString() : m_model.anchorDisplayName(preference.anchorId);
        map.insert(QStringLiteral("sentence"), Preferences::sentence(preference, area));
        list.append(map);
    }
    return list;
}

QString WorkspaceStore::anchorPurpose(const QString &anchorId) const
{
    const Preferences::Effects effects = preferenceEffects();
    for (auto it = effects.purposeAreas.cbegin(); it != effects.purposeAreas.cend(); ++it) {
        if (it.value() == anchorId) {
            return it.key();
        }
    }
    return {};
}

QVariantMap WorkspaceStore::addPreference(Preferences::Preference preference)
{
    if (preference.key == QStringLiteral("area_purpose")) {
        preference.value = Preferences::normalizedPurpose(preference.value);
        preference.collectionId = m_collectionId;
    }
    const QString problem = Preferences::validate(preference.key, preference.value, preference.anchorId);
    if (!problem.isEmpty()) {
        return failure(QStringLiteral("INVALID_ARGUMENT"), problem);
    }
    if (!preference.anchorId.isEmpty() && !m_model.anchor(preference.anchorId)) {
        return failure(QStringLiteral("NOT_FOUND"), QStringLiteral("That panel isn't open."));
    }
    preference.id = QUuid::createUuid().toString(QUuid::Id128).left(12);
    preference.setAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    Preferences::put(m_preferences, preference);
    savePreferences();
    emit preferencesChanged();
    const QString area = preference.anchorId.isEmpty() ? QString() : m_model.anchorDisplayName(preference.anchorId);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), preference.id},
        {QStringLiteral("sentence"), Preferences::sentence(preference, area)},
    };
}

QVariantMap WorkspaceStore::setPreference(const QString &key, const QString &value, const QString &anchorId, bool lasting)
{
    Preferences::Preference preference;
    preference.key = key;
    preference.value = value;
    preference.anchorId = anchorId;
    preference.lasting = lasting;
    preference.setBy = QStringLiteral("person");
    return addPreference(preference);
}

bool WorkspaceStore::forgetPreference(const QString &id)
{
    const qsizetype before = m_preferences.size();
    m_preferences.erase(
        std::remove_if(m_preferences.begin(), m_preferences.end(), [&](const Preferences::Preference &preference) { return preference.id == id; }),
        m_preferences.end()
    );
    if (m_preferences.size() == before) {
        return false;
    }
    savePreferences();
    emit preferencesChanged();
    return true;
}

QVariantMap WorkspaceStore::preferenceForAgent(const QVariantMap &request)
{
    const QString op = request.value(QStringLiteral("op")).toString();
    const QString chatId = request.value(QStringLiteral("chatId")).toString();

    if (op == QStringLiteral("list")) {
        return {{QStringLiteral("ok"), true}, {QStringLiteral("preferences"), workspacePreferences()}};
    }

    const QString key = request.value(QStringLiteral("preference")).toString();
    QString anchorId;
    if (key == QStringLiteral("area_purpose")) {
        const QString area = request.value(QStringLiteral("area")).toString();
        const Intents::AreaMatch match = Intents::resolveArea(m_model, area.isEmpty() ? QStringLiteral("this") : area, agentEnvironment(false));
        if (!match.isValid()) {
            return failure(QStringLiteral("NOT_FOUND"), QStringLiteral("There's no panel matching “%1”.").arg(area));
        }
        anchorId = match.anchorId;
    }

    if (op == QStringLiteral("clear")) {
        QStringList forgotten;
        const QString purpose = Preferences::normalizedPurpose(request.value(QStringLiteral("value")).toString());
        for (const QVariant &value : workspacePreferences()) {
            const QVariantMap map = value.toMap();
            if (map.value(QStringLiteral("key")).toString() != key) {
                continue;
            }
            if (key == QStringLiteral("area_purpose") && map.value(QStringLiteral("anchorId")).toString() != anchorId
                && map.value(QStringLiteral("value")).toString() != purpose) {
                continue;
            }
            forgetPreference(map.value(QStringLiteral("id")).toString());
            forgotten.append(map.value(QStringLiteral("sentence")).toString());
        }
        if (forgotten.isEmpty()) {
            return failure(QStringLiteral("NOT_FOUND"), QStringLiteral("There's no such preference to forget."));
        }
        const QString summary = QStringLiteral("Forgot: ") + forgotten.join(QStringLiteral("; "));
        logAgent(summary, QStringLiteral("applied"), chatId);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("notes"), QStringList{summary}}};
    }

    if (op != QStringLiteral("set")) {
        return failure(QStringLiteral("UNSUPPORTED"), QStringLiteral("Preferences can be set, cleared or listed."));
    }

    // Only a statement of preference ("always", "from now on", "I like my")
    // lasts; anything else is for this session.
    const bool wantsLasting = request.value(QStringLiteral("lasting")).toBool();
    const bool mayLast = request.value(QStringLiteral("lastingAllowed")).toBool();
    Preferences::Preference preference;
    preference.key = key;
    preference.value = request.value(QStringLiteral("value")).toString();
    preference.anchorId = anchorId;
    preference.lasting = wantsLasting && mayLast;
    preference.setBy = QStringLiteral("agent");
    QVariantMap result = addPreference(preference);
    if (!result.value(QStringLiteral("ok")).toBool()) {
        return result;
    }
    QStringList notes{result.value(QStringLiteral("sentence")).toString() + (preference.lasting ? QStringLiteral(" (always)") : QStringLiteral(" (this session)"))};
    if (wantsLasting && !mayLast) {
        notes.append(QStringLiteral("Kept for this session only: the person didn't say to always do it"));
    }
    logAgent(notes.first(), QStringLiteral("applied"), chatId);
    result.insert(QStringLiteral("status"), QStringLiteral("applied"));
    result.insert(QStringLiteral("notes"), notes);
    return result;
}

// ---- named layouts ----------------------------------------------------------

QString WorkspaceStore::layoutIdNamed(const QString &name) const
{
    const QString wanted = name.trimmed();
    for (const QVariant &value : savedLayouts()) {
        if (value.toMap().value(QStringLiteral("name")).toString().compare(wanted, Qt::CaseInsensitive) == 0) {
            return value.toMap().value(QStringLiteral("id")).toString();
        }
    }
    for (const Presets::Preset &preset : Presets::all()) {
        if (preset.name.compare(wanted, Qt::CaseInsensitive) == 0 || preset.id.compare(wanted, Qt::CaseInsensitive) == 0) {
            return preset.id;
        }
    }
    return {};
}

QVariantList WorkspaceStore::layoutsForAgent() const
{
    QVariantList layouts;
    for (const QVariant &value : savedLayouts()) {
        const QVariantMap layout = value.toMap();
        layouts.append(QVariantMap{
            {QStringLiteral("name"), layout.value(QStringLiteral("name"))},
            {QStringLiteral("kind"), QStringLiteral("saved by the person")},
            {QStringLiteral("current"), layout.value(QStringLiteral("id")).toString() == m_currentLayoutId},
        });
    }
    for (const Presets::Preset &preset : Presets::all()) {
        layouts.append(QVariantMap{
            {QStringLiteral("name"), preset.name},
            {QStringLiteral("kind"), QStringLiteral("built in")},
            {QStringLiteral("description"), preset.description},
            {QStringLiteral("current"), preset.id == m_currentLayoutId},
        });
    }
    return layouts;
}

QVariantMap WorkspaceStore::layoutForAgent(const QVariantMap &request)
{
    const QString op = request.value(QStringLiteral("op")).toString();
    const QString name = request.value(QStringLiteral("name")).toString().trimmed();
    const QString chatId = request.value(QStringLiteral("chatId")).toString();

    if (op == QStringLiteral("list")) {
        return {{QStringLiteral("ok"), true}, {QStringLiteral("layouts"), layoutsForAgent()}};
    }

    if (op == QStringLiteral("save")) {
        if (name.isEmpty()) {
            return failure(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("A layout needs a name."));
        }
        for (const Presets::Preset &preset : Presets::all()) {
            if (preset.name.compare(name, Qt::CaseInsensitive) == 0) {
                return failure(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("“%1” is a built-in layout; pick another name.").arg(preset.name));
            }
        }
        // A saved layout is only replaced when the person said so.
        if (!layoutIdNamed(name).isEmpty() && !request.value(QStringLiteral("replace")).toBool()) {
            return failure(QStringLiteral("EXISTS"), QStringLiteral("There's already a layout called “%1”. Ask whether to replace it.").arg(name));
        }
        const QString saved = saveLayout(name);
        if (saved.isEmpty()) {
            return failure(QStringLiteral("FAILED"), QStringLiteral("The layout couldn't be saved."));
        }
        // Saved on purpose: this arrangement is no longer temporary.
        keepArrangement();
        const QString summary = QStringLiteral("Saved the current arrangement as the “%1” layout").arg(name);
        logAgent(summary, QStringLiteral("applied"), chatId);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("notes"), QStringList{summary}}};
    }

    if (op == QStringLiteral("rename") || op == QStringLiteral("delete")) {
        const QString layoutId = layoutIdNamed(name);
        if (layoutId.isEmpty() || !layoutId.startsWith(QStringLiteral("user."))) {
            return failure(QStringLiteral("NOT_FOUND"), QStringLiteral("There's no saved layout called “%1” (built-in layouts can't be changed).").arg(name));
        }
        if (op == QStringLiteral("rename")) {
            const QString newName = request.value(QStringLiteral("newName")).toString().trimmed();
            if (newName.isEmpty() || !layoutIdNamed(newName).isEmpty()) {
                return failure(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("Give a new name that isn't taken."));
            }
            if (!renameLayout(layoutId, newName)) {
                return failure(QStringLiteral("FAILED"), QStringLiteral("The layout couldn't be renamed."));
            }
            const QString summary = QStringLiteral("Renamed the “%1” layout to “%2”").arg(name, newName);
            logAgent(summary, QStringLiteral("applied"), chatId);
            return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("notes"), QStringList{summary}}};
        }
        if (!deleteLayout(layoutId)) {
            return failure(QStringLiteral("FAILED"), QStringLiteral("The layout couldn't be deleted."));
        }
        const QString summary = QStringLiteral("Deleted the “%1” layout").arg(name);
        logAgent(summary, QStringLiteral("applied"), chatId);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("notes"), QStringList{summary}}};
    }

    return failure(QStringLiteral("UNSUPPORTED"), QStringLiteral("Layouts can be listed, applied, saved, renamed or deleted."));
}
