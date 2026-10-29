#include "Core/Domains/Shell/shell_navigation.h"

#include <algorithm>

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

bool knownPanel(const QString &panelId, const QList<RailItem> &custom = {})
{
    const RailItem *item = railItem(panelId, custom);
    return item && item->isPanel();
}

int customNumber(const QString &id)
{
    bool ok = false;
    const int number = id.mid(RailIds::CustomPrefix.size()).toInt(&ok);
    return ok ? number : 0;
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
        {QStringLiteral("custom"), custom},
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

bool isCustomRailId(const QString &id)
{
    return id.startsWith(RailIds::CustomPrefix) && customNumber(id) > 0;
}

RailItem customTab(const QString &id, const QString &label)
{
    RailItem item = panel(id, label, QStringLiteral("grid"),
                          QStringLiteral("A side panel of your own: dock tabs and panels into it."), -1, true);
    item.custom = true;
    return item;
}

QList<RailItem> railItemsWith(const QList<RailItem> &custom)
{
    QList<RailItem> items = railItems();
    items.append(custom);
    return items;
}

QVariantList railItemsVariant(const QList<RailItem> &custom)
{
    QVariantList list;
    for (const RailItem &item : railItemsWith(custom)) {
        list.append(item.toVariant());
    }
    return list;
}

const RailItem *railItem(const QString &id, const QList<RailItem> &custom)
{
    if (const RailItem *item = railItem(id)) {
        return item;
    }
    for (const RailItem &item : custom) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

QList<RailItem> addCustomTab(const QList<RailItem> &custom, const QString &label, QString *createdId)
{
    int next = 1;
    for (const RailItem &item : custom) {
        next = std::max(next, customNumber(item.id) + 1);
    }
    const QString id = RailIds::CustomPrefix + QString::number(next);
    const QString name = label.trimmed().isEmpty() ? QStringLiteral("Tab %1").arg(next) : label.trimmed();
    QList<RailItem> out = custom;
    out.append(customTab(id, name));
    if (createdId) {
        *createdId = id;
    }
    return out;
}

QList<RailItem> removeCustomTab(const QList<RailItem> &custom, const QString &id)
{
    QList<RailItem> out;
    for (const RailItem &item : custom) {
        if (item.id != id) {
            out.append(item);
        }
    }
    return out;
}

QList<RailItem> renameCustomTab(const QList<RailItem> &custom, const QString &id, const QString &label)
{
    QList<RailItem> out = custom;
    for (RailItem &item : out) {
        if (item.id == id && !label.trimmed().isEmpty()) {
            item.label = label.trimmed();
        }
    }
    return out;
}

QVariantList customTabsToVariant(const QList<RailItem> &custom)
{
    QVariantList list;
    for (const RailItem &item : custom) {
        list.append(QVariantMap{{QStringLiteral("id"), item.id}, {QStringLiteral("label"), item.label}});
    }
    return list;
}

QList<RailItem> customTabsFromVariant(const QVariantList &value)
{
    QList<RailItem> out;
    QStringList seen;
    for (const QVariant &entry : value) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        if (!isCustomRailId(id) || seen.contains(id)) {
            continue;
        }
        seen.append(id);
        const QString label = map.value(QStringLiteral("label")).toString().trimmed();
        out.append(customTab(id, label.isEmpty() ? QStringLiteral("Tab %1").arg(out.size() + 1) : label));
    }
    return out;
}

NavigationState NavigationState::fromVariant(const QVariantMap &value, const QList<RailItem> &custom)
{
    NavigationState state;
    const QString destination = value.value(QStringLiteral("destination")).toString();
    state.destination = destination == Destinations::Workspace ? Destinations::Workspace : Destinations::Home;
    const QString panelId = value.value(QStringLiteral("panelId")).toString();
    state.panelId = knownPanel(panelId, custom) ? panelId : QString(RailIds::Navigator);
    state.panelOpen = value.contains(QStringLiteral("panelOpen"))
        ? value.value(QStringLiteral("panelOpen")).toBool()
        : true;
    return state;
}

NavigationState activate(const NavigationState &state, const QString &railId, const QList<RailItem> &custom)
{
    const RailItem *item = railItem(railId, custom);
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

NavigationState showPanel(const NavigationState &state, const QString &panelId, const QList<RailItem> &custom)
{
    if (!knownPanel(panelId, custom)) {
        return state;
    }
    NavigationState next = state;
    next.destination = Destinations::Workspace;
    next.panelId = panelId;
    next.panelOpen = true;
    return next;
}

NavigationState restorePanel(const NavigationState &state, const QString &panelId, bool open, const QList<RailItem> &custom)
{
    NavigationState next = state;
    next.panelId = knownPanel(panelId, custom) ? panelId : QString(RailIds::Navigator);
    next.panelOpen = open;
    return next;
}

NavigationState forgetPanel(const NavigationState &state, const QString &panelId)
{
    NavigationState next = state;
    if (next.panelId == panelId) {
        next.panelId = RailIds::Navigator;
    }
    return next;
}
} // namespace Shell
