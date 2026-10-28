#pragma once

#include "Core/Domains/Home/home_items.h"
#include "Core/Domains/Home/home_modules.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class QSettings;

// QML's view of the person's Home: which modules show under the composer and
// in what order, what they pinned, and what happened lately. Registered as
// the `HomeStore` singleton.
//
// Only Home's own configuration lives here. Where the person is (Home or a
// project) stays in Shell, conversations in ChatStore, projects in
// CollectionStore and layouts in Docking; pins and events only point at them.
class HomeStore final : public QObject
{
    Q_OBJECT
    // [{ id, typeId, visible, span, title, description, icon, available,
    //    builtIn, expandViewType, permissions }] in order.
    Q_PROPERTY(QVariantList modules READ modules NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList moduleTypes READ moduleTypes NOTIFY moduleTypesChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    // The preset the layout matches, or "custom".
    Q_PROPERTY(QString presetId READ presetId NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged)
    Q_PROPERTY(int pinCapacity READ pinCapacity CONSTANT)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(int unreadActivity READ unreadActivity NOTIFY activityChanged)

public:
    // settings: where to save; null uses the app's QSettings. Not owned.
    explicit HomeStore(QSettings *settings = nullptr, QObject *parent = nullptr);
    ~HomeStore() override;

    [[nodiscard]] QVariantList modules() const;
    [[nodiscard]] QVariantList moduleTypes() const;
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QString presetId() const;
    [[nodiscard]] QVariantList pins() const { return m_pins.toVariant(); }
    [[nodiscard]] int pinCapacity() const { return m_pins.capacity(); }
    [[nodiscard]] QVariantList activity() const { return m_activity.toVariant(); }
    [[nodiscard]] int unreadActivity() const { return m_activity.unreadCount(); }

    [[nodiscard]] const Home::HomeLayout &layout() const { return m_layout; }
    [[nodiscard]] const Home::ModuleRegistry &registry() const { return m_registry; }

    // Where a plugin will register its compact Home presentation. Refused
    // descriptors return false with a sentence in `error`.
    bool registerModuleType(const Home::ModuleDescriptor &descriptor, QString *error = nullptr);

    // ---- layout ---------------------------------------------------------
    Q_INVOKABLE bool setModuleVisible(const QString &moduleId, bool visible);
    Q_INVOKABLE bool moveModule(const QString &moduleId, int toIndex);
    Q_INVOKABLE bool setModuleSpan(const QString &moduleId, const QString &span);
    Q_INVOKABLE QString addModule(const QString &typeId);
    Q_INVOKABLE bool applyPreset(const QString &presetId);
    Q_INVOKABLE void restoreDefault();

    // ---- pins -----------------------------------------------------------
    // pin: { kind, id, collectionId, libraryId, title, subtitle }
    Q_INVOKABLE bool pin(const QVariantMap &pin);
    Q_INVOKABLE bool unpin(const QString &kind, const QString &id);
    Q_INVOKABLE bool togglePin(const QVariantMap &pin);
    Q_INVOKABLE bool movePin(const QString &kind, const QString &id, int toIndex);
    Q_INVOKABLE bool renamePin(const QString &kind, const QString &id, const QString &title, const QString &subtitle = {});
    [[nodiscard]] Q_INVOKABLE bool isPinned(const QString &kind, const QString &id) const;

    // ---- activity -------------------------------------------------------
    // event: { kind, key, title, detail, unread (default true), target:
    // {kind, id, collectionId, libraryId}, artifact: {...} }. Returns the
    // new event's id (never the target's), or empty.
    Q_INVOKABLE QString noteActivity(const QVariantMap &event);
    Q_INVOKABLE bool dismissActivity(const QString &eventId);
    Q_INVOKABLE void markActivityRead();
    Q_INVOKABLE void clearActivity();

    // A project was removed: drop its pins and events.
    Q_INVOKABLE void forgetCollection(const QString &collectionId);

signals:
    void layoutChanged();
    void moduleTypesChanged();
    void pinsChanged();
    void activityChanged();

private:
    void commitLayout(const Home::HomeLayout &next);
    void savePins();
    void saveActivity();

    QSettings *m_settings = nullptr;
    std::unique_ptr<QSettings> m_ownedSettings;
    Home::ModuleRegistry m_registry;
    Home::HomeLayout m_layout;
    Home::Pins m_pins;
    Home::ActivityLog m_activity;
};
