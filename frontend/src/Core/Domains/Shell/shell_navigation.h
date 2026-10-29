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
// A person's own tabs: "navigation.custom.1", "navigation.custom.2", ...
inline const QString CustomPrefix = QStringLiteral("navigation.custom.");
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
    bool custom = false;      // a tab the person added (and can rename or delete)

    [[nodiscard]] bool isPanel() const { return destination == Destinations::Workspace; }
    [[nodiscard]] QVariantMap toVariant() const;
};

// Built-in rail, top to bottom. Home comes first.
[[nodiscard]] const QList<RailItem> &railItems();
[[nodiscard]] const RailItem *railItem(const QString &id);
[[nodiscard]] QString panelIdForLegacyIndex(int index); // unknown: Navigator
[[nodiscard]] int legacyIndexForPanel(const QString &panelId); // unknown: 0
[[nodiscard]] QVariantList railItemsVariant();

// ---- a person's own tabs ------------------------------------------------
//
// Added under the built-in panels. Each is an empty side panel that tabs
// and panels can be docked into (the docking workspace keeps what is in
// it, per project). Saved once for the app.
[[nodiscard]] bool isCustomRailId(const QString &id);
[[nodiscard]] RailItem customTab(const QString &id, const QString &label);
// Built-in items, then the person's own.
[[nodiscard]] QList<RailItem> railItemsWith(const QList<RailItem> &custom);
[[nodiscard]] QVariantList railItemsVariant(const QList<RailItem> &custom);
[[nodiscard]] const RailItem *railItem(const QString &id, const QList<RailItem> &custom);
// A new tab with the next free id; an empty label becomes "Tab N".
[[nodiscard]] QList<RailItem> addCustomTab(const QList<RailItem> &custom, const QString &label, QString *createdId = nullptr);
[[nodiscard]] QList<RailItem> removeCustomTab(const QList<RailItem> &custom, const QString &id);
[[nodiscard]] QList<RailItem> renameCustomTab(const QList<RailItem> &custom, const QString &id, const QString &label);
[[nodiscard]] QVariantList customTabsToVariant(const QList<RailItem> &custom);
// Forgiving: skips entries that aren't custom ids, and duplicates.
[[nodiscard]] QList<RailItem> customTabsFromVariant(const QVariantList &value);

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
    [[nodiscard]] static NavigationState fromVariant(const QVariantMap &value, const QList<RailItem> &custom = {});

    bool operator==(const NavigationState &other) const = default;
};

// A rail click.
//   Home              go Home; on Home already, back to the workspace
//   a panel, on Home  back to the workspace with that panel open
//   the open panel    closes the side panel
//   another panel     opens it
// Unknown ids change nothing.
[[nodiscard]] NavigationState activate(const NavigationState &state, const QString &railId, const QList<RailItem> &custom = {});
[[nodiscard]] NavigationState goHome(const NavigationState &state);
[[nodiscard]] NavigationState returnToWorkspace(const NavigationState &state);
// Opens a panel in the workspace (revealing a file, say). Leaves Home.
[[nodiscard]] NavigationState showPanel(const NavigationState &state, const QString &panelId, const QList<RailItem> &custom = {});
// Per-project panel state coming back after a project switch. Doesn't
// change the destination.
[[nodiscard]] NavigationState restorePanel(const NavigationState &state, const QString &panelId, bool open,
                                           const QList<RailItem> &custom = {});
// A tab was deleted: if it was showing, the Navigator shows instead.
[[nodiscard]] NavigationState forgetPanel(const NavigationState &state, const QString &panelId);
} // namespace Shell
