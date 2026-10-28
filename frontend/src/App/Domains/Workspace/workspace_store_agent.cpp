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
constexpr qsizetype kMaximumAgentSteps = 20;
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
    if (!currentLayoutName().isEmpty()) {
        description.insert(QStringLiteral("currentLayout"), currentLayoutName());
    }
    return description;
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
            } else if (wanted == chatId) {
                // This conversation lives in the bottom workbench.
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

    const WorkspaceModel::Data before = m_model.data();
    const bool surfaceCollapsedBefore = m_commandSurfaceCollapsed;
    const QString surfaceViewBefore = m_commandSurfaceView;
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
        AgentStep step;
        step.before = before;
        step.opened = outcome.opened;
        step.summary = summary;
        step.surfaceTouched = surfaceChanges;
        step.surfaceCollapsedBefore = surfaceCollapsedBefore;
        step.surfaceViewBefore = surfaceViewBefore;
        step.startedTask = m_baseline.isEmpty();
        m_agentUndo.append(step);
        rememberArrangement(before, outcome.opened, summary);
        while (m_agentUndo.size() > kMaximumAgentSteps) {
            m_agentUndo.removeFirst();
        }
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
    if (holdBack.isEmpty() && preferenceEffects().onlyWhenAsked && !request.value(QStringLiteral("explicit")).toBool()) {
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
        if (!saveAs.isEmpty() && suggested.value(QStringLiteral("ok")).toBool()) {
            suggested.insert(QStringLiteral("layout"), QStringLiteral("Not saved as “%1”: the change wasn't applied.").arg(saveAs));
        }
        return suggested;
    }

    const bool focusWanted = request.value(QStringLiteral("focus")).toBool() || action == QStringLiteral("focus");
    QVariantMap result = applyAgentIntent(intent, chatId, focusWanted && !userBusy());

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

bool WorkspaceStore::restoreTo(const WorkspaceModel::Data &arrangement, const QStringList &opened, QString *code, QString *error)
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

    const Arrangements::Result result = Arrangements::restore(m_model, arrangement, closable, sizes, m_metrics);
    if (!result.ok) {
        *code = result.code;
        *error = result.message;
        return false;
    }

    publish();
    for (const ViewRecord &closed : std::as_const(closing)) {
        emit viewClosed(closed.id, closed.typeId, resourceToMap(closed.resource));
    }
    return true;
}

// The bottom panel goes back to how the earliest undone step found it.
void WorkspaceStore::restoreCommandSurface(qsizetype firstStep)
{
    for (qsizetype index = firstStep; index < m_agentUndo.size(); ++index) {
        const AgentStep &step = m_agentUndo.at(index);
        if (step.surfaceTouched) {
            emit commandSurfaceRequested(step.surfaceViewBefore, !step.surfaceCollapsedBefore);
            return;
        }
    }
}

bool WorkspaceStore::restoreAgentSteps(qsizetype count, QString *code, QString *error)
{
    if (m_agentUndo.isEmpty() || count <= 0) {
        *code = QStringLiteral("NOTHING_TO_RESTORE");
        *error = QStringLiteral("There's no agent change to undo.");
        return false;
    }

    count = std::min(count, m_agentUndo.size());
    const qsizetype first = m_agentUndo.size() - count;
    QStringList opened;
    bool endsTask = false;
    for (qsizetype index = first; index < m_agentUndo.size(); ++index) {
        opened.append(m_agentUndo.at(index).opened);
        endsTask = endsTask || m_agentUndo.at(index).startedTask;
    }

    if (!restoreTo(m_agentUndo.at(first).before, opened, code, error)) {
        return false;
    }

    restoreCommandSurface(first);
    m_agentUndo.remove(first, count);
    if (endsTask) {
        // Back to where the task started: nothing temporary is left.
        forgetArrangement();
    }
    emit agentChanged();
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
        while (m_baselineChanges.size() > kMaximumAgentSteps) {
            m_baselineChanges.removeFirst();
        }
    }

    const QJsonObject record{
        {QStringLiteral("baseline"), m_baseline},
        {QStringLiteral("opened"), QJsonArray::fromStringList(m_baselineOpened)},
        {QStringLiteral("changes"), QJsonArray::fromStringList(m_baselineChanges)},
        {QStringLiteral("since"), m_baselineSince},
    };
    m_settings.setValue(arrangementKey(m_collectionId), QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
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
        return restoreAgentSteps(m_agentUndo.size(), code, error);
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

    QStringList opened = m_baselineOpened;
    for (const AgentStep &step : std::as_const(m_agentUndo)) {
        opened.append(step.opened);
    }
    if (!restoreTo(then.data(), opened, code, error)) {
        return false;
    }

    restoreCommandSurface(0);
    m_agentUndo.clear();
    forgetArrangement();
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
    if (m_baseline.isEmpty() && m_agentUndo.isEmpty()) {
        return;
    }
    m_agentUndo.clear();
    forgetArrangement();
    logAgent(QStringLiteral("Kept the current arrangement"), QStringLiteral("kept"), {});
    emit agentChanged();
}

bool WorkspaceStore::undoAgentChange()
{
    QString code;
    QString error;
    if (!restoreAgentSteps(1, &code, &error)) {
        emit operationFailed(error);
        return false;
    }
    logAgent(QStringLiteral("Undid the last agent change"), QStringLiteral("restored"), {});
    return true;
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
        if (!applyLayout(layoutId)) {
            return failure(QStringLiteral("FAILED"), QStringLiteral("The layout couldn't be applied."));
        }
        AgentStep step;
        step.before = before;
        step.summary = QStringLiteral("Switched to the “%1” layout").arg(currentLayoutName());
        step.startedTask = m_baseline.isEmpty();
        m_agentUndo.append(step);
        rememberArrangement(before, {}, step.summary);
        logAgent(step.summary, QStringLiteral("applied"), chatId);
        emit agentChanged();
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("notes"), QStringList{step.summary}},
        };
    }

    QString code;
    QString error;
    const bool restored = scope == QStringLiteral("before_task") || m_agentUndo.isEmpty()
        ? restoreArrangement(&code, &error)
        : restoreAgentSteps(1, &code, &error);
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
