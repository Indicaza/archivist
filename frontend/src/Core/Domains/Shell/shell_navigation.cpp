#include "Core/Domains/Shell/shell_navigation.h"

namespace Shell
{
namespace
{
RailItem panel(
    const QString &id,
    const QString &label,
    const QString &icon,
    const QString &description,
    int legacyIndex,
    bool placeholder
)
{
    RailItem item;
    item.id = id;
    item.label = label;
    item.icon = icon;
    item.description = description;
    item.destination = Destinations::Workspace;
    item.legacyIndex = legacyIndex;
    item.placeholder = placeholder;
    return item;
}

bool knownPanel(const QString &panelId)
{
    const RailItem *item = railItem(panelId);
    return item && item->isPanel();
}
} // namespace

QVariantMap RailItem::toVariant() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("description"), description},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("legacyIndex"), legacyIndex},
        {QStringLiteral("isPanel"), isPanel()},
        {QStringLiteral("placeholder"), placeholder},
    };
}

const QList<RailItem> &railItems()
{
    static const QList<RailItem> items = [] {
        RailItem home;
        home.id = RailIds::Home;
        home.label = QStringLiteral("Home");
        home.icon = QStringLiteral("home");
        home.description = QStringLiteral(
            "Start a conversation, reopen recent work and switch projects. Click again to go back to your workspace."
        );
        home.destination = Destinations::Home;

        return QList<RailItem>{
            home,
            panel(RailIds::Navigator, QStringLiteral("Workspace Navigator"), QStringLiteral("workspace"),
                  QStringLiteral("The project's libraries, files and chats."), 0, false),
            panel(RailIds::Archive, QStringLiteral("Archived Libraries"), QStringLiteral("archive"),
                  QStringLiteral("Libraries put away from this project."), 1, true),
            panel(RailIds::Search, QStringLiteral("Library Search"), QStringLiteral("search"),
                  QStringLiteral("Search the project's libraries."), 2, true),
            panel(RailIds::Plugins, QStringLiteral("Plugins"), QStringLiteral("plugin"),
                  QStringLiteral("Installed plugins."), 3, true),
            panel(RailIds::Tools, QStringLiteral("Tools"), QStringLiteral("tools"),
                  QStringLiteral("Developer tools."), 4, true),
        };
    }();
    return items;
}

const RailItem *railItem(const QString &id)
{
    for (const RailItem &item : railItems()) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

QString panelIdForLegacyIndex(int index)
{
    for (const RailItem &item : railItems()) {
        if (item.isPanel() && item.legacyIndex == index) {
            return item.id;
        }
    }
    return RailIds::Navigator;
}

int legacyIndexForPanel(const QString &panelId)
{
    const RailItem *item = railItem(panelId);
    return item && item->isPanel() ? item->legacyIndex : 0;
}

QVariantList railItemsVariant()
{
    QVariantList list;
    for (const RailItem &item : railItems()) {
        list.append(item.toVariant());
    }
    return list;
}

QVariantMap NavigationState::toVariant() const
{
    return {
        {QStringLiteral("destination"), destination},
        {QStringLiteral("panelId"), panelId},
        {QStringLiteral("panelOpen"), panelOpen},
    };
}

NavigationState NavigationState::fromVariant(const QVariantMap &value)
{
    NavigationState state;
    const QString destination = value.value(QStringLiteral("destination")).toString();
    state.destination = destination == Destinations::Workspace ? Destinations::Workspace : Destinations::Home;
    const QString panelId = value.value(QStringLiteral("panelId")).toString();
    state.panelId = knownPanel(panelId) ? panelId : QString(RailIds::Navigator);
    state.panelOpen = value.contains(QStringLiteral("panelOpen"))
        ? value.value(QStringLiteral("panelOpen")).toBool()
        : true;
    return state;
}

NavigationState activate(const NavigationState &state, const QString &railId)
{
    const RailItem *item = railItem(railId);
    if (!item) {
        return state;
    }

    if (!item->isPanel()) {
        return state.homeActive() ? returnToWorkspace(state) : goHome(state);
    }

    NavigationState next = state;
    if (state.homeActive()) {
        next.destination = Destinations::Workspace;
        next.panelId = item->id;
        next.panelOpen = true;
        return next;
    }

    if (state.panelOpen && state.panelId == item->id) {
        next.panelOpen = false;
        return next;
    }

    next.panelId = item->id;
    next.panelOpen = true;
    return next;
}

NavigationState goHome(const NavigationState &state)
{
    NavigationState next = state;
    next.destination = Destinations::Home;
    return next;
}

NavigationState returnToWorkspace(const NavigationState &state)
{
    NavigationState next = state;
    next.destination = Destinations::Workspace;
    return next;
}

NavigationState showPanel(const NavigationState &state, const QString &panelId)
{
    if (!knownPanel(panelId)) {
        return state;
    }
    NavigationState next = state;
    next.destination = Destinations::Workspace;
    next.panelId = panelId;
    next.panelOpen = true;
    return next;
}

NavigationState restorePanel(const NavigationState &state, const QString &panelId, bool open)
{
    NavigationState next = state;
    next.panelId = knownPanel(panelId) ? panelId : QString(RailIds::Navigator);
    next.panelOpen = open;
    return next;
}
} // namespace Shell
