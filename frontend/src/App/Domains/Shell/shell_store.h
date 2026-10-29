#pragma once

#include "Core/Domains/Shell/recent_items.h"
#include "Core/Domains/Shell/shell_navigation.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class QSettings;

// QML's view of where the person is (Home or the workspace), the rail, the
// recent items and the Home draft. Registered as the `Shell` singleton.
//
// The rules live in Core/Domains/Shell; this only holds the state, saves it
// and tells QML. The side panel's state is saved per project by the shell
// QML (as before) and comes back through restorePanel.
class ShellStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString destination READ destination NOTIFY navigationChanged)
    Q_PROPERTY(bool homeActive READ homeActive NOTIFY navigationChanged)
    Q_PROPERTY(QString panelId READ panelId NOTIFY navigationChanged)
    Q_PROPERTY(int panelIndex READ panelIndex NOTIFY navigationChanged)
    Q_PROPERTY(bool panelOpen READ panelOpen NOTIFY navigationChanged)
    Q_PROPERTY(bool panelShown READ panelShown NOTIFY navigationChanged)
    Q_PROPERTY(QVariantList railItems READ railItems NOTIFY railItemsChanged)
    // Ids of every side panel tab, built-in and the person's own.
    Q_PROPERTY(QStringList panelIds READ panelIds NOTIFY railItemsChanged)
    Q_PROPERTY(QVariantList recentItems READ recentItems NOTIFY recentItemsChanged)
    Q_PROPERTY(QString homeDraft READ homeDraft WRITE setHomeDraft NOTIFY homeDraftChanged)

public:
    // settings: where to save; null uses the app's QSettings. Not owned.
    explicit ShellStore(QSettings *settings = nullptr, QObject *parent = nullptr);
    ~ShellStore() override;

    [[nodiscard]] QString destination() const { return m_state.destination; }
    [[nodiscard]] bool homeActive() const { return m_state.homeActive(); }
    [[nodiscard]] QString panelId() const { return m_state.panelId; }
    [[nodiscard]] int panelIndex() const { return Shell::legacyIndexForPanel(m_state.panelId); }
    [[nodiscard]] bool panelOpen() const { return m_state.panelOpen; }
    [[nodiscard]] bool panelShown() const { return m_state.panelShown(); }
    [[nodiscard]] QVariantList railItems() const { return Shell::railItemsVariant(m_customTabs); }
    [[nodiscard]] QStringList panelIds() const;
    [[nodiscard]] QVariantList recentItems() const { return m_recents.toVariant(); }
    [[nodiscard]] QString homeDraft() const { return m_homeDraft; }
    void setHomeDraft(const QString &draft);

    [[nodiscard]] const Shell::NavigationState &state() const { return m_state; }
    [[nodiscard]] const Shell::RecentItems &recents() const { return m_recents; }

    // A rail click, by id ("navigation.home", "navigation.navigator").
    Q_INVOKABLE void activate(const QString &railId);
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void returnToWorkspace();
    Q_INVOKABLE void toggleHome();
    Q_INVOKABLE void showPanel(const QString &panelId);
    Q_INVOKABLE void setPanelOpen(bool open);
    Q_INVOKABLE void restorePanel(const QString &panelId, bool open);
    Q_INVOKABLE void restorePanelIndex(int legacyIndex, bool open);
    [[nodiscard]] Q_INVOKABLE QVariantMap railItem(const QString &railId) const;

    // A person's own side panel tabs. Adding one opens it. Removing one only
    // removes the tab: whoever calls it first moves what is docked there.
    Q_INVOKABLE QString addRailTab(const QString &label = {});
    Q_INVOKABLE bool removeRailTab(const QString &railId);
    Q_INVOKABLE bool renameRailTab(const QString &railId, const QString &label);

    // item: { kind, id, collectionId, libraryId, title, subtitle, details }
    Q_INVOKABLE bool noteRecent(const QVariantMap &item);
    Q_INVOKABLE bool forgetRecent(const QString &kind, const QString &id);
    Q_INVOKABLE int forgetCollection(const QString &collectionId);
    [[nodiscard]] Q_INVOKABLE QVariantList recent(const QString &kind, int limit = 0) const;

signals:
    void navigationChanged();
    void recentItemsChanged();
    void homeDraftChanged();
    void railItemsChanged();

private:
    void apply(const Shell::NavigationState &next);
    void saveNavigation();
    void saveRecents();
    void saveCustomTabs();

    QSettings *m_settings = nullptr;
    std::unique_ptr<QSettings> m_ownedSettings;
    Shell::NavigationState m_state;
    QList<Shell::RailItem> m_customTabs;
    Shell::RecentItems m_recents;
    QString m_homeDraft;
};
