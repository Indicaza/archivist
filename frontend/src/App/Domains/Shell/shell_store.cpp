#include "App/Domains/Shell/shell_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace
{
const QString DestinationKey = QStringLiteral("shell/destination");
const QString RecentsKey = QStringLiteral("shell/recents");
const QString HomeDraftKey = QStringLiteral("shell/homeDraft");

QVariantList readJsonList(const QVariant &stored)
{
    const QJsonDocument document = QJsonDocument::fromJson(stored.toString().toUtf8());
    return document.isArray() ? document.array().toVariantList() : QVariantList{};
}
} // namespace

ShellStore::ShellStore(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (!m_settings) {
        m_ownedSettings = std::make_unique<QSettings>();
        m_settings = m_ownedSettings.get();
    }

    // First launch (nothing saved) starts on Home.
    Shell::NavigationState saved;
    saved.destination = m_settings->value(DestinationKey, Shell::Destinations::Home).toString();
    m_state = Shell::NavigationState::fromVariant(saved.toVariant());
    m_recents = Shell::RecentItems::fromVariant(readJsonList(m_settings->value(RecentsKey)));
    m_homeDraft = m_settings->value(HomeDraftKey).toString();
}

ShellStore::~ShellStore() = default;

void ShellStore::setHomeDraft(const QString &draft)
{
    if (draft == m_homeDraft) {
        return;
    }
    m_homeDraft = draft;
    if (draft.isEmpty()) {
        m_settings->remove(HomeDraftKey);
    } else {
        m_settings->setValue(HomeDraftKey, draft);
    }
    emit homeDraftChanged();
}

void ShellStore::activate(const QString &railId)
{
    apply(Shell::activate(m_state, railId));
}

void ShellStore::goHome()
{
    apply(Shell::goHome(m_state));
}

void ShellStore::returnToWorkspace()
{
    apply(Shell::returnToWorkspace(m_state));
}

void ShellStore::toggleHome()
{
    apply(m_state.homeActive() ? Shell::returnToWorkspace(m_state) : Shell::goHome(m_state));
}

void ShellStore::showPanel(const QString &panelId)
{
    apply(Shell::showPanel(m_state, panelId));
}

void ShellStore::setPanelOpen(bool open)
{
    Shell::NavigationState next = m_state;
    next.panelOpen = open;
    apply(next);
}

void ShellStore::restorePanel(const QString &panelId, bool open)
{
    apply(Shell::restorePanel(m_state, panelId, open));
}

void ShellStore::restorePanelIndex(int legacyIndex, bool open)
{
    restorePanel(Shell::panelIdForLegacyIndex(legacyIndex), open);
}

QVariantMap ShellStore::railItem(const QString &railId) const
{
    const Shell::RailItem *item = Shell::railItem(railId);
    return item ? item->toVariant() : QVariantMap{};
}

bool ShellStore::noteRecent(const QVariantMap &input)
{
    Shell::RecentItem item = Shell::RecentItem::fromVariant(input);
    if (!item.isValid()) {
        return false;
    }

    // Seeing the same thing again (every focus change reports it) only
    // refreshes its time; nothing on screen changes, so nothing is published.
    const QList<Shell::RecentItem> &current = m_recents.items();
    if (!current.isEmpty()) {
        const Shell::RecentItem &front = current.first();
        const bool same = front.key() == item.key()
            && (item.title.isEmpty() || item.title == front.title)
            && (item.subtitle.isEmpty() || item.subtitle == front.subtitle)
            && (item.collectionId.isEmpty() || item.collectionId == front.collectionId);
        if (same) {
            return true;
        }
    }

    if (!m_recents.note(item)) {
        return false;
    }
    saveRecents();
    emit recentItemsChanged();
    return true;
}

bool ShellStore::forgetRecent(const QString &kind, const QString &id)
{
    if (!m_recents.forget(kind, id)) {
        return false;
    }
    saveRecents();
    emit recentItemsChanged();
    return true;
}

int ShellStore::forgetCollection(const QString &collectionId)
{
    const int removed = m_recents.forgetCollection(collectionId);
    if (removed > 0) {
        saveRecents();
        emit recentItemsChanged();
    }
    return removed;
}

QVariantList ShellStore::recent(const QString &kind, int limit) const
{
    QVariantList list;
    for (const Shell::RecentItem &item : m_recents.items(kind, limit)) {
        list.append(item.toVariant());
    }
    return list;
}

void ShellStore::apply(const Shell::NavigationState &next)
{
    if (next == m_state) {
        return;
    }
    const bool destinationChanged = next.destination != m_state.destination;
    m_state = next;
    if (destinationChanged) {
        saveNavigation();
    }
    emit navigationChanged();
}

void ShellStore::saveNavigation()
{
    m_settings->setValue(DestinationKey, m_state.destination);
}

void ShellStore::saveRecents()
{
    m_settings->setValue(
        RecentsKey,
        QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_recents.toVariant())).toJson(QJsonDocument::Compact))
    );
}
