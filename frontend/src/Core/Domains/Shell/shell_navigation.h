#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Where the person is in the app, and what the left-edge rail shows.
//
//   destination   home | workspace
//                 Home is the chat-first start page. The workspace is the
//                 project with its panels, editors and the bottom panel.
//   panel         which rail item fills the side panel in the workspace
//   panelOpen     whether the side panel is open
//
// Rail items have stable ids ("navigation.home", "navigation.navigator").
// Those ids are what saved state, Spaces, plugins and the AI's UI guidance
// refer to. Each panel also keeps the index it had before ids existed, so
// shell state saved by older builds still reads back.
//
// Leaving for Home never changes the panel or closes anything. Coming back
// puts the workspace exactly as it was.
namespace Shell
{
namespace Destinations
{
inline const QString Home = QStringLiteral("home");
inline const QString Workspace = QStringLiteral("workspace");
} // namespace Destinations

namespace RailIds
{
inline const QString Home = QStringLiteral("navigation.home");
inline const QString Navigator = QStringLiteral("navigation.navigator");
inline const QString Archive = QStringLiteral("navigation.archive");
inline const QString Search = QStringLiteral("navigation.search");
inline const QString Plugins = QStringLiteral("navigation.plugins");
inline const QString Tools = QStringLiteral("navigation.tools");
} // namespace RailIds

struct RailItem
{
    QString id;
    QString label;       // tooltip and accessible name
    QString icon;        // AppIcon name
    QString description; // one sentence on what it does, for guidance
    QString destination; // Home: "home"; panels: "workspace"
    int legacyIndex = -1; // panels only: the index saved before ids existed
    bool placeholder = false; // panel has no content yet

    [[nodiscard]] bool isPanel() const { return destination == Destinations::Workspace; }
    [[nodiscard]] QVariantMap toVariant() const;
};

// Built-in rail, top to bottom. Home comes first.
[[nodiscard]] const QList<RailItem> &railItems();
[[nodiscard]] const RailItem *railItem(const QString &id);
[[nodiscard]] QString panelIdForLegacyIndex(int index); // unknown: Navigator
[[nodiscard]] int legacyIndexForPanel(const QString &panelId); // unknown: 0
[[nodiscard]] QVariantList railItemsVariant();

struct NavigationState
{
    QString destination = Destinations::Home;
    QString panelId = RailIds::Navigator;
    bool panelOpen = true;

    [[nodiscard]] bool homeActive() const { return destination == Destinations::Home; }
    // The side panel is on screen (it's never shown on Home).
    [[nodiscard]] bool panelShown() const { return !homeActive() && panelOpen; }

    [[nodiscard]] QVariantMap toVariant() const;
    // Forgiving: unknown destinations go Home, unknown panels to Navigator.
    [[nodiscard]] static NavigationState fromVariant(const QVariantMap &value);

    bool operator==(const NavigationState &other) const = default;
};

// A rail click.
//   Home              go Home; on Home already, back to the workspace
//   a panel, on Home  back to the workspace with that panel open
//   the open panel    closes the side panel
//   another panel     opens it
// Unknown ids change nothing.
[[nodiscard]] NavigationState activate(const NavigationState &state, const QString &railId);
[[nodiscard]] NavigationState goHome(const NavigationState &state);
[[nodiscard]] NavigationState returnToWorkspace(const NavigationState &state);
// Opens a panel in the workspace (revealing a file, say). Leaves Home.
[[nodiscard]] NavigationState showPanel(const NavigationState &state, const QString &panelId);
// Per-project panel state coming back after a project switch. Doesn't
// change the destination.
[[nodiscard]] NavigationState restorePanel(const NavigationState &state, const QString &panelId, bool open);
} // namespace Shell
