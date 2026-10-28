#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// The composer: how the conversation's input presents itself (its mode) and
// where it sits (its placement). Two separate things, kept apart here.
//
// The composer never owns the conversation. Drafts live in Chats::DraftBook,
// messages and streaming in ChatStore; this is only presentation, so moving
// or switching the composer can't lose or resend anything.
//
// Pure data and rules, no Qt Quick.
namespace Composer
{
namespace Modes
{
inline const QString Compact = QStringLiteral("compact");     // ChatGPT-style input
inline const QString Workbench = QStringLiteral("workbench"); // chat, terminal, problems
} // namespace Modes

namespace Placements
{
inline const QString Bottom = QStringLiteral("bottom");     // over the bottom of the workspace
inline const QString Anchor = QStringLiteral("anchor");     // docked under a chat Anchor's messages
inline const QString Floating = QStringLiteral("floating"); // a free surface (not yet presented)
} // namespace Placements

// A presentation of the composer. Plugins will register more (writing,
// production, agent operations); each says which placements it supports.
struct Mode
{
    QString id;
    QString label;
    QString icon;
    QString description;
    QStringList placements; // where it can sit
    bool builtIn = false;

    [[nodiscard]] bool allows(const QString &placement) const { return placements.contains(placement); }
    [[nodiscard]] QVariantMap toVariant() const;
};

class ModeRegistry
{
public:
    // Compact (every placement) and Workbench (the bottom only).
    ModeRegistry();

    // Refuses an empty or taken id, or a mode with no placement.
    bool registerMode(Mode mode, QString *error = nullptr);
    [[nodiscard]] const Mode *mode(const QString &id) const;
    [[nodiscard]] bool has(const QString &id) const { return mode(id) != nullptr; }
    [[nodiscard]] const QList<Mode> &modes() const { return m_modes; }
    [[nodiscard]] QVariantList toVariant() const;

private:
    QList<Mode> m_modes;
};

struct Placement
{
    QString kind = Placements::Bottom;
    QString anchorId;   // kind == Anchor
    QRectF floatingRect; // kind == Floating, in workspace coordinates

    [[nodiscard]] bool isBottom() const { return kind == Placements::Bottom; }
    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static Placement fromVariant(const QVariantMap &value);
    bool operator==(const Placement &other) const = default;
};

// What the workspace knows about its anchors, for fallbacks.
struct Environment
{
    std::function<bool(const QString &anchorId)> anchorShowsChat; // exists and holds a chat view
};

struct State
{
    // The mode chosen for this project; empty follows the space default.
    QString chosenMode;
    // Where the compact composer lives. A mode that can't sit there (the
    // workbench is always at the bottom) shows at the bottom without
    // forgetting it, so collapsing goes back to the user's spot.
    Placement placement;
    // Where it was before it floated, to return to.
    Placement beforeFloating;
    // Per conversation: the mode that conversation always opens in.
    QVariantMap conversationModes; // chatId -> mode id

    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static State fromVariant(const QVariantMap &value);
    bool operator==(const State &other) const = default;
};

// The mode in effect: the conversation's own choice, else the project's,
// else the space default, else compact. Unknown modes are skipped.
[[nodiscard]] QString effectiveMode(const State &state, const ModeRegistry &registry, const QString &chatId,
                                    const QString &spaceDefault = Modes::Compact);

// Where the composer actually shows for `mode`: its placement when the mode
// supports it, else the bottom.
[[nodiscard]] Placement effectivePlacement(const State &state, const ModeRegistry &registry, const QString &mode);

// Repairs stale references: an anchor that's gone or no longer shows a chat,
// a floating rect that's empty, a conversation mode that doesn't exist.
// Returns true when anything changed.
bool normalize(State &state, const ModeRegistry &registry, const Environment &environment);

// ---- operations (each returns false when nothing changes) ----------------

bool chooseMode(State &state, const ModeRegistry &registry, const QString &mode);
// Pins `mode` for one conversation; an empty mode clears the pin.
bool setConversationMode(State &state, const ModeRegistry &registry, const QString &chatId, const QString &mode);
bool forgetConversation(State &state, const QString &chatId);
bool dockInAnchor(State &state, const Environment &environment, const QString &anchorId);
bool moveToBottom(State &state);
bool floatAt(State &state, const QRectF &rect);
// Back to where it was before floating (the bottom when that's gone).
bool returnFromFloating(State &state, const Environment &environment);
// The default: the bottom, following the space default mode. Per
// conversation pins are kept.
bool restoreDefault(State &state);

// ---- semantic commands -----------------------------------------------------

// Stable ids for the composer's controls and operations, so the shell,
// shortcuts, menus and (later) AI guidance all name them the same way.
namespace Commands
{
inline const QString Expand = QStringLiteral("composer.expand");
inline const QString Collapse = QStringLiteral("composer.collapse");
inline const QString ToggleMode = QStringLiteral("composer.toggle");
inline const QString SelectModePrefix = QStringLiteral("composer.mode."); // + mode id
inline const QString DockInConversation = QStringLiteral("composer.place.anchor");
inline const QString MoveToBottom = QStringLiteral("composer.place.bottom");
inline const QString Float = QStringLiteral("composer.place.floating");
inline const QString Restore = QStringLiteral("composer.restore");
// Keep the current mode for this conversation whatever the space does.
inline const QString RememberForConversation = QStringLiteral("composer.remember");
} // namespace Commands

struct Command
{
    QString id;
    QString label;
    QString description;
    QString shortcut; // portable text; empty when none
    [[nodiscard]] QVariantMap toVariant() const;
};

[[nodiscard]] QList<Command> commands();
[[nodiscard]] QVariantList commandsVariant();
} // namespace Composer
