// The composer's presentation for the current project: which mode it's in
// and where it sits. Saved per project; placement moves are workspace
// history steps, so undo puts the composer back too.
//
// Conversation state is elsewhere on purpose (drafts: ChatDrafts, messages
// and streaming: ChatStore). Nothing here can lose or resend a message.

#include "App/Domains/Workspace/workspace_store.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace Workspace;

namespace
{
const QString ChatViewType = QStringLiteral("archivist.chat");
}

QString WorkspaceStore::composerKey(const QString &collectionId) const
{
    return QStringLiteral("workspace/collections/%1/composer").arg(collectionId);
}

bool WorkspaceStore::anchorShowsChat(const QString &anchorId) const
{
    const AnchorRecord *anchor = m_model.anchor(anchorId);
    if (!anchor) {
        return false;
    }
    for (const QString &viewId : anchor->views) {
        const ViewRecord *view = m_model.view(viewId);
        if (view && view->typeId == ChatViewType) {
            return true;
        }
    }
    return false;
}

Composer::Environment WorkspaceStore::composerEnvironment() const
{
    Composer::Environment environment;
    environment.anchorShowsChat = [this](const QString &anchorId) { return anchorShowsChat(anchorId); };
    return environment;
}

QVariantMap WorkspaceStore::composer() const
{
    QVariantMap map = m_composer.toVariant();
    map.insert(QStringLiteral("spaceDefault"), m_composerSpaceDefault);
    return map;
}

QString WorkspaceStore::composerMode(const QString &chatId) const
{
    return Composer::effectiveMode(m_composer, m_composerModes, chatId, m_composerSpaceDefault);
}

QVariantMap WorkspaceStore::composerPlacement(const QString &mode) const
{
    return Composer::effectivePlacement(m_composer, m_composerModes, mode).toVariant();
}

bool WorkspaceStore::composerModeAllows(const QString &mode, const QString &placement) const
{
    const Composer::Mode *entry = m_composerModes.mode(mode);
    return entry && entry->allows(placement);
}

void WorkspaceStore::setComposerState(const Composer::State &state)
{
    if (state == m_composer) {
        return;
    }
    m_composer = state;
    saveComposer();
    emit composerChanged();
}

bool WorkspaceStore::setComposerMode(const QString &mode)
{
    Composer::State next = m_composer;
    if (!Composer::chooseMode(next, m_composerModes, mode)) {
        return false;
    }
    setComposerState(next);
    return true;
}

bool WorkspaceStore::setConversationComposerMode(const QString &chatId, const QString &mode)
{
    Composer::State next = m_composer;
    if (!Composer::setConversationMode(next, m_composerModes, chatId, mode)) {
        return false;
    }
    setComposerState(next);
    return true;
}

void WorkspaceStore::forgetComposerConversation(const QString &chatId)
{
    Composer::State next = m_composer;
    if (Composer::forgetConversation(next, chatId)) {
        setComposerState(next);
    }
}

bool WorkspaceStore::setComposerSpaceDefault(const QString &mode)
{
    if (!m_composerModes.has(mode) || mode == m_composerSpaceDefault) {
        return false;
    }
    m_composerSpaceDefault = mode;
    saveComposer();
    emit composerChanged();
    return true;
}

bool WorkspaceStore::changeComposerPlacement(const QString &summary, const std::function<bool(Composer::State &)> &change)
{
    HistoryStep step = historyStep(summary);
    step.composerTouched = true;
    Composer::State next = m_composer;
    if (!change(next)) {
        return false;
    }
    recordStep(std::move(step));
    setComposerState(next);
    return true;
}

bool WorkspaceStore::dockComposer(const QString &anchorId)
{
    // A content-locked anchor takes nothing new, the composer included.
    const AnchorRecord *anchor = m_model.anchor(anchorId);
    if (anchor && anchor->contentLocked) {
        return false;
    }
    const Composer::Environment environment = composerEnvironment();
    return changeComposerPlacement(QStringLiteral("Docked the message box in a conversation"), [&](Composer::State &state) {
        return Composer::dockInAnchor(state, environment, anchorId);
    });
}

bool WorkspaceStore::moveComposerToBottom()
{
    return changeComposerPlacement(QStringLiteral("Moved the message box to the bottom"), [](Composer::State &state) {
        return Composer::moveToBottom(state);
    });
}

bool WorkspaceStore::floatComposer(double x, double y, double width, double height)
{
    const QRectF rect(x, y, width, height);
    const bool alreadyFloating = m_composer.placement.kind == Composer::Placements::Floating;
    if (alreadyFloating) {
        // Dragging a floating composer around isn't a step of its own.
        Composer::State next = m_composer;
        if (!Composer::floatAt(next, rect)) {
            return false;
        }
        setComposerState(next);
        return true;
    }
    return changeComposerPlacement(QStringLiteral("Floated the message box"), [&](Composer::State &state) {
        return Composer::floatAt(state, rect);
    });
}

bool WorkspaceStore::returnComposerFromFloating()
{
    const Composer::Environment environment = composerEnvironment();
    return changeComposerPlacement(QStringLiteral("Docked the message box again"), [&](Composer::State &state) {
        return Composer::returnFromFloating(state, environment);
    });
}

bool WorkspaceStore::restoreComposer()
{
    return changeComposerPlacement(QStringLiteral("Put the message box back in its default place"), [](Composer::State &state) {
        return Composer::restoreDefault(state);
    });
}

bool WorkspaceStore::registerComposerMode(const Composer::Mode &mode, QString *error)
{
    if (!m_composerModes.registerMode(mode, error)) {
        return false;
    }
    emit composerModesChanged();
    return true;
}

void WorkspaceStore::normalizeComposer()
{
    Composer::State next = m_composer;
    if (Composer::normalize(next, m_composerModes, composerEnvironment())) {
        setComposerState(next);
    }
}

void WorkspaceStore::loadComposer()
{
    Composer::State loaded;
    QString spaceDefault = Composer::Modes::Compact;
    if (!m_collectionId.isEmpty()) {
        const QJsonObject object = QJsonDocument::fromJson(m_settings.value(composerKey(m_collectionId)).toString().toUtf8()).object();
        const QVariantMap map = object.toVariantMap();
        loaded = Composer::State::fromVariant(map);
        const QString savedDefault = map.value(QStringLiteral("spaceDefault")).toString();
        if (m_composerModes.has(savedDefault)) {
            spaceDefault = savedDefault;
        }
    }
    Composer::normalize(loaded, m_composerModes, composerEnvironment());
    const bool changed = !(loaded == m_composer) || spaceDefault != m_composerSpaceDefault;
    m_composer = loaded;
    m_composerSpaceDefault = spaceDefault;
    if (changed) {
        emit composerChanged();
    }
}

void WorkspaceStore::saveComposer()
{
    if (m_collectionId.isEmpty()) {
        return;
    }
    QVariantMap map = m_composer.toVariant();
    map.insert(QStringLiteral("spaceDefault"), m_composerSpaceDefault);
    m_settings.setValue(composerKey(m_collectionId),
                        QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact)));
}
