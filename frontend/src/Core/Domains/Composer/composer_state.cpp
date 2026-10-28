#include "Core/Domains/Composer/composer_state.h"

namespace Composer
{
namespace
{
const QStringList AllPlacements{Placements::Bottom, Placements::Anchor, Placements::Floating};

bool knownPlacement(const QString &kind)
{
    return AllPlacements.contains(kind);
}

// A floating composer smaller than this is a broken record, not a choice.
constexpr qreal MinimumFloatingWidth = 280;
constexpr qreal MinimumFloatingHeight = 60;

bool usableRect(const QRectF &rect)
{
    return rect.width() >= MinimumFloatingWidth && rect.height() >= MinimumFloatingHeight;
}
} // namespace

QVariantMap Mode::toVariant() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("description"), description},
        {QStringLiteral("placements"), placements},
        {QStringLiteral("builtIn"), builtIn},
    };
}

ModeRegistry::ModeRegistry()
{
    m_modes.append(Mode{Modes::Compact, QStringLiteral("Chat"), QStringLiteral("chat"),
                        QStringLiteral("A simple message box over the conversation."), AllPlacements, true});
    m_modes.append(Mode{Modes::Workbench, QStringLiteral("Workbench"), QStringLiteral("terminal"),
                        QStringLiteral("Chat with terminals and problems, docked at the bottom."),
                        {Placements::Bottom}, true});
}

bool ModeRegistry::registerMode(Mode mode, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    mode.id = mode.id.trimmed();
    if (mode.id.isEmpty()) {
        return fail(QStringLiteral("A composer mode needs an id."));
    }
    if (has(mode.id)) {
        return fail(QStringLiteral("A composer mode called \"%1\" already exists.").arg(mode.id));
    }
    QStringList placements;
    for (const QString &placement : mode.placements) {
        if (knownPlacement(placement) && !placements.contains(placement)) {
            placements.append(placement);
        }
    }
    if (placements.isEmpty()) {
        return fail(QStringLiteral("A composer mode must support at least one placement."));
    }
    mode.placements = placements;
    mode.builtIn = false;
    if (mode.label.trimmed().isEmpty()) {
        mode.label = mode.id;
    }
    m_modes.append(mode);
    return true;
}

const Mode *ModeRegistry::mode(const QString &id) const
{
    for (const Mode &entry : m_modes) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

QVariantList ModeRegistry::toVariant() const
{
    QVariantList list;
    for (const Mode &entry : m_modes) {
        list.append(entry.toVariant());
    }
    return list;
}

QVariantMap Placement::toVariant() const
{
    QVariantMap map{{QStringLiteral("kind"), kind}};
    if (kind == Placements::Anchor) {
        map.insert(QStringLiteral("anchorId"), anchorId);
    }
    if (kind == Placements::Floating) {
        map.insert(QStringLiteral("x"), floatingRect.x());
        map.insert(QStringLiteral("y"), floatingRect.y());
        map.insert(QStringLiteral("width"), floatingRect.width());
        map.insert(QStringLiteral("height"), floatingRect.height());
    }
    return map;
}

Placement Placement::fromVariant(const QVariantMap &value)
{
    Placement placement;
    const QString kind = value.value(QStringLiteral("kind")).toString();
    if (!knownPlacement(kind)) {
        return placement;
    }
    placement.kind = kind;
    if (kind == Placements::Anchor) {
        placement.anchorId = value.value(QStringLiteral("anchorId")).toString();
        if (placement.anchorId.isEmpty()) {
            return Placement{};
        }
    }
    if (kind == Placements::Floating) {
        placement.floatingRect = QRectF(value.value(QStringLiteral("x")).toDouble(), value.value(QStringLiteral("y")).toDouble(),
                                        value.value(QStringLiteral("width")).toDouble(),
                                        value.value(QStringLiteral("height")).toDouble());
    }
    return placement;
}

QVariantMap State::toVariant() const
{
    QVariantMap map{
        {QStringLiteral("placement"), placement.toVariant()},
    };
    if (!chosenMode.isEmpty()) {
        map.insert(QStringLiteral("mode"), chosenMode);
    }
    if (!beforeFloating.isBottom()) {
        map.insert(QStringLiteral("beforeFloating"), beforeFloating.toVariant());
    }
    if (!conversationModes.isEmpty()) {
        map.insert(QStringLiteral("conversationModes"), conversationModes);
    }
    return map;
}

State State::fromVariant(const QVariantMap &value)
{
    State state;
    state.chosenMode = value.value(QStringLiteral("mode")).toString();
    state.placement = Placement::fromVariant(value.value(QStringLiteral("placement")).toMap());
    state.beforeFloating = Placement::fromVariant(value.value(QStringLiteral("beforeFloating")).toMap());
    if (state.beforeFloating.kind == Placements::Floating) {
        state.beforeFloating = Placement{};
    }
    const QVariantMap modes = value.value(QStringLiteral("conversationModes")).toMap();
    for (auto it = modes.constBegin(); it != modes.constEnd(); ++it) {
        const QString mode = it.value().toString();
        if (!it.key().isEmpty() && !mode.isEmpty()) {
            state.conversationModes.insert(it.key(), mode);
        }
    }
    return state;
}

QString effectiveMode(const State &state, const ModeRegistry &registry, const QString &chatId, const QString &spaceDefault)
{
    const QString pinned = chatId.isEmpty() ? QString() : state.conversationModes.value(chatId).toString();
    for (const QString &candidate : {pinned, state.chosenMode, spaceDefault}) {
        if (!candidate.isEmpty() && registry.has(candidate)) {
            return candidate;
        }
    }
    return Modes::Compact;
}

Placement effectivePlacement(const State &state, const ModeRegistry &registry, const QString &mode)
{
    const Mode *entry = registry.mode(mode);
    if (entry && entry->allows(state.placement.kind)) {
        return state.placement;
    }
    return Placement{};
}

bool normalize(State &state, const ModeRegistry &registry, const Environment &environment)
{
    const State before = state;
    auto anchorUsable = [&](const QString &anchorId) {
        return !anchorId.isEmpty() && environment.anchorShowsChat && environment.anchorShowsChat(anchorId);
    };
    if (!state.chosenMode.isEmpty() && !registry.has(state.chosenMode)) {
        state.chosenMode.clear();
    }
    if (state.placement.kind == Placements::Anchor && !anchorUsable(state.placement.anchorId)) {
        state.placement = Placement{};
    }
    if (state.placement.kind == Placements::Floating && !usableRect(state.placement.floatingRect)) {
        state.placement = state.beforeFloating;
    }
    if (state.placement.kind != Placements::Floating) {
        state.beforeFloating = Placement{};
    } else if (state.beforeFloating.kind == Placements::Anchor && !anchorUsable(state.beforeFloating.anchorId)) {
        state.beforeFloating = Placement{};
    }
    // Normalize again in case beforeFloating was the stale anchor we fell to.
    if (state.placement.kind == Placements::Anchor && !anchorUsable(state.placement.anchorId)) {
        state.placement = Placement{};
    }
    for (auto it = state.conversationModes.begin(); it != state.conversationModes.end();) {
        if (!registry.has(it.value().toString())) {
            it = state.conversationModes.erase(it);
        } else {
            ++it;
        }
    }
    return !(before == state);
}

bool chooseMode(State &state, const ModeRegistry &registry, const QString &mode)
{
    if (!registry.has(mode) || state.chosenMode == mode) {
        return false;
    }
    state.chosenMode = mode;
    return true;
}

bool setConversationMode(State &state, const ModeRegistry &registry, const QString &chatId, const QString &mode)
{
    if (chatId.isEmpty()) {
        return false;
    }
    if (mode.isEmpty()) {
        return state.conversationModes.remove(chatId) > 0;
    }
    if (!registry.has(mode) || state.conversationModes.value(chatId).toString() == mode) {
        return false;
    }
    state.conversationModes.insert(chatId, mode);
    return true;
}

bool forgetConversation(State &state, const QString &chatId)
{
    return state.conversationModes.remove(chatId) > 0;
}

bool dockInAnchor(State &state, const Environment &environment, const QString &anchorId)
{
    if (anchorId.isEmpty() || !environment.anchorShowsChat || !environment.anchorShowsChat(anchorId)) {
        return false;
    }
    Placement next;
    next.kind = Placements::Anchor;
    next.anchorId = anchorId;
    if (state.placement == next) {
        return false;
    }
    state.placement = next;
    state.beforeFloating = Placement{};
    return true;
}

bool moveToBottom(State &state)
{
    if (state.placement.isBottom()) {
        return false;
    }
    state.placement = Placement{};
    state.beforeFloating = Placement{};
    return true;
}

bool floatAt(State &state, const QRectF &rect)
{
    if (!usableRect(rect)) {
        return false;
    }
    if (state.placement.kind == Placements::Floating) {
        if (state.placement.floatingRect == rect) {
            return false;
        }
        state.placement.floatingRect = rect;
        return true;
    }
    state.beforeFloating = state.placement;
    state.placement = Placement{Placements::Floating, {}, rect};
    return true;
}

bool returnFromFloating(State &state, const Environment &environment)
{
    if (state.placement.kind != Placements::Floating) {
        return false;
    }
    Placement back = state.beforeFloating;
    if (back.kind == Placements::Anchor
        && (!environment.anchorShowsChat || !environment.anchorShowsChat(back.anchorId))) {
        back = Placement{};
    }
    state.placement = back;
    state.beforeFloating = Placement{};
    return true;
}

bool restoreDefault(State &state)
{
    if (state.placement.isBottom() && state.chosenMode.isEmpty() && state.beforeFloating.isBottom()) {
        return false;
    }
    state.placement = Placement{};
    state.beforeFloating = Placement{};
    state.chosenMode.clear();
    return true;
}

QVariantMap Command::toVariant() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("description"), description},
        {QStringLiteral("shortcut"), shortcut},
    };
}

QList<Command> commands()
{
    return {
        {Commands::Expand, QStringLiteral("Expand workbench"),
         QStringLiteral("Open chat, terminals and problems at the bottom."), QStringLiteral("Ctrl+J")},
        {Commands::Collapse, QStringLiteral("Collapse to chat"),
         QStringLiteral("Fold the workbench back into the message box. Terminals keep running."), QStringLiteral("Ctrl+J")},
        {Commands::ToggleMode, QStringLiteral("Expand or collapse"),
         QStringLiteral("Switch between the message box and the workbench."), QStringLiteral("Ctrl+J")},
        {Commands::DockInConversation, QStringLiteral("Dock in conversation"),
         QStringLiteral("Put the message box under the conversation's messages."), QString()},
        {Commands::MoveToBottom, QStringLiteral("Move to bottom"),
         QStringLiteral("Float the message box over the bottom of the workspace."), QString()},
        {Commands::Float, QStringLiteral("Float"),
         QStringLiteral("Let the message box float where you put it."), QString()},
        {Commands::Restore, QStringLiteral("Restore default position"),
         QStringLiteral("Back to the bottom, in this space's default mode."), QStringLiteral("Ctrl+Shift+J")},
    };
}

QVariantList commandsVariant()
{
    QVariantList list;
    for (const Command &command : commands()) {
        list.append(command.toVariant());
    }
    return list;
}
} // namespace Composer
