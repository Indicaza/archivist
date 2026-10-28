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
#include <QEvent>
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
            if (!docked.isEmpty()) {
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
        m_agentUndo.append(step);
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

    if (!holdBack.isEmpty()) {
        return suggestAgentIntent(intent, chatId, holdBack);
    }

    const bool focusWanted = request.value(QStringLiteral("focus")).toBool() || action == QStringLiteral("focus");
    return applyAgentIntent(intent, chatId, focusWanted && !userBusy());
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

bool WorkspaceStore::restoreAgentSteps(qsizetype count, QString *code, QString *error)
{
    if (m_agentUndo.isEmpty() || count <= 0) {
        *code = QStringLiteral("NOTHING_TO_RESTORE");
        *error = QStringLiteral("There's no agent change to undo.");
        return false;
    }

    count = std::min(count, m_agentUndo.size());
    const qsizetype first = m_agentUndo.size() - count;
    const AgentStep &oldest = m_agentUndo.at(first);
    QStringList opened;
    for (qsizetype index = first; index < m_agentUndo.size(); ++index) {
        opened.append(m_agentUndo.at(index).opened);
    }

    QHash<QString, QSizeF> sizes;
    for (const QString &surfaceId : m_model.surfaceIds()) {
        sizes.insert(surfaceId, surfaceSize(surfaceId));
    }

    QList<ViewRecord> closing;
    const QSet<QString> closable = closableAgentViews(opened);
    for (const QString &viewId : closable) {
        closing.append(*m_model.view(viewId));
    }

    const Arrangements::Result result = Arrangements::restore(m_model, oldest.before, closable, sizes, m_metrics);
    if (!result.ok) {
        *code = result.code;
        *error = result.message;
        return false;
    }

    bool surfaceTouched = false;
    bool collapsed = false;
    QString view;
    for (qsizetype index = first; index < m_agentUndo.size(); ++index) {
        if (m_agentUndo.at(index).surfaceTouched) {
            surfaceTouched = true;
            collapsed = m_agentUndo.at(index).surfaceCollapsedBefore;
            view = m_agentUndo.at(index).surfaceViewBefore;
            break;
        }
    }

    m_agentUndo.remove(first, count);
    publish();
    for (const ViewRecord &closed : std::as_const(closing)) {
        emit viewClosed(closed.id, closed.typeId, resourceToMap(closed.resource));
    }
    if (surfaceTouched) {
        emit commandSurfaceRequested(view, !collapsed);
    }
    emit agentChanged();
    return true;
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
        QString layoutId;
        for (const QVariant &value : savedLayouts()) {
            if (value.toMap().value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
                layoutId = value.toMap().value(QStringLiteral("id")).toString();
            }
        }
        for (const Presets::Preset &preset : Presets::all()) {
            if (layoutId.isEmpty() && preset.name.compare(name, Qt::CaseInsensitive) == 0) {
                layoutId = preset.id;
            }
        }
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
        m_agentUndo.append(step);
        logAgent(step.summary, QStringLiteral("applied"), chatId);
        emit agentChanged();
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("status"), QStringLiteral("applied")},
            {QStringLiteral("notes"), QStringList{step.summary}},
        };
    }

    const qsizetype count = scope == QStringLiteral("before_task") ? m_agentUndo.size() : 1;
    QString code;
    QString error;
    if (!restoreAgentSteps(count, &code, &error)) {
        return failure(code, error);
    }

    const QString summary = scope == QStringLiteral("before_task")
        ? QStringLiteral("Put the workspace back the way it was")
        : QStringLiteral("Undid the last workspace change");
    logAgent(summary, QStringLiteral("restored"), chatId);
    emit agentArranged(summary);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("status"), QStringLiteral("applied")},
        {QStringLiteral("notes"), QStringList{summary}},
    };
}
