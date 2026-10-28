#include "App/Domains/Home/home_store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

namespace
{
const QString LayoutKey = QStringLiteral("home/layout");
const QString PinsKey = QStringLiteral("home/pins");
const QString ActivityKey = QStringLiteral("home/activity");

QVariant readJson(const QVariant &stored)
{
    const QJsonDocument document = QJsonDocument::fromJson(stored.toString().toUtf8());
    if (document.isArray()) {
        return document.array().toVariantList();
    }
    if (document.isObject()) {
        return document.object().toVariantMap();
    }
    return {};
}

QString compactJson(const QVariantList &list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
}

QString compactJson(const QVariantMap &map)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}
} // namespace

HomeStore::HomeStore(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_registry(Home::ModuleRegistry::withBuiltIns())
{
    if (!m_settings) {
        m_ownedSettings = std::make_unique<QSettings>();
        m_settings = m_ownedSettings.get();
    }
    m_layout = Home::HomeLayout::fromVariant(readJson(m_settings->value(LayoutKey)).toMap(), m_registry);
    m_pins = Home::Pins::fromVariant(readJson(m_settings->value(PinsKey)).toList());
    m_activity = Home::ActivityLog::fromVariant(readJson(m_settings->value(ActivityKey)).toList());
}

HomeStore::~HomeStore() = default;

QVariantList HomeStore::modules() const
{
    QVariantList list;
    for (const Home::ModuleEntry &entry : m_layout.modules()) {
        QVariantMap value = entry.toVariant();
        const Home::ModuleDescriptor *descriptor = m_registry.find(entry.typeId);
        value.insert(QStringLiteral("available"), descriptor != nullptr);
        if (descriptor) {
            const QVariantMap described = descriptor->toVariant();
            for (auto it = described.cbegin(); it != described.cend(); ++it) {
                if (!value.contains(it.key())) {
                    value.insert(it.key(), it.value());
                }
            }
        } else {
            value.insert(QStringLiteral("title"), QStringLiteral("Unavailable module"));
            value.insert(QStringLiteral("description"), QStringLiteral("The plugin that provides this isn't installed."));
        }
        list.append(value);
    }
    return list;
}

QVariantList HomeStore::moduleTypes() const
{
    QVariantList list;
    for (const Home::ModuleDescriptor &descriptor : m_registry.types()) {
        list.append(descriptor.toVariant());
    }
    return list;
}

QVariantList HomeStore::presets() const
{
    QVariantList list;
    for (const Home::PresetInfo &info : Home::presets()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), info.id},
            {QStringLiteral("title"), info.title},
            {QStringLiteral("description"), info.description},
        });
    }
    return list;
}

QString HomeStore::presetId() const
{
    return m_layout.matchingPreset(m_registry);
}

bool HomeStore::registerModuleType(const Home::ModuleDescriptor &descriptor, QString *error)
{
    if (!m_registry.registerType(descriptor, error)) {
        return false;
    }
    emit moduleTypesChanged();
    // A module already placed for this type becomes available again.
    if (m_layout.indexOf(descriptor.typeId) >= 0) {
        emit layoutChanged();
    }
    return true;
}

bool HomeStore::setModuleVisible(const QString &moduleId, bool visible)
{
    Home::HomeLayout next = m_layout;
    if (!next.setVisible(moduleId, visible)) {
        return false;
    }
    commitLayout(next);
    return true;
}

bool HomeStore::moveModule(const QString &moduleId, int toIndex)
{
    Home::HomeLayout next = m_layout;
    if (!next.move(moduleId, toIndex)) {
        return false;
    }
    commitLayout(next);
    return true;
}

bool HomeStore::setModuleSpan(const QString &moduleId, const QString &span)
{
    Home::HomeLayout next = m_layout;
    if (!next.setSpan(moduleId, span)) {
        return false;
    }
    commitLayout(next);
    return true;
}

QString HomeStore::addModule(const QString &typeId)
{
    Home::HomeLayout next = m_layout;
    const QString id = next.add(typeId, m_registry);
    if (!id.isEmpty()) {
        commitLayout(next);
    }
    return id;
}

bool HomeStore::applyPreset(const QString &presetId)
{
    bool known = false;
    for (const Home::PresetInfo &info : Home::presets()) {
        known = known || info.id == presetId;
    }
    if (!known) {
        return false;
    }
    const Home::HomeLayout next = Home::HomeLayout::preset(presetId, m_registry);
    if (next == m_layout) {
        return false;
    }
    commitLayout(next);
    return true;
}

void HomeStore::restoreDefault()
{
    const Home::HomeLayout next = Home::HomeLayout::defaults(m_registry);
    if (next != m_layout) {
        commitLayout(next);
    }
}

void HomeStore::commitLayout(const Home::HomeLayout &next)
{
    m_layout = next;
    m_settings->setValue(LayoutKey, compactJson(m_layout.toVariant()));
    emit layoutChanged();
}

bool HomeStore::pin(const QVariantMap &input)
{
    Home::Pin pin = Home::Pin::fromVariant(input);
    pin.pinnedAt = QDateTime::currentMSecsSinceEpoch();
    if (pin.title.isEmpty()) {
        return false;
    }
    if (!m_pins.pin(pin)) {
        return false;
    }
    savePins();
    return true;
}

bool HomeStore::unpin(const QString &kind, const QString &id)
{
    if (!m_pins.unpin(kind, id)) {
        return false;
    }
    savePins();
    return true;
}

bool HomeStore::togglePin(const QVariantMap &input)
{
    const QString kind = input.value(QStringLiteral("kind")).toString().trimmed();
    const QString id = input.value(QStringLiteral("id")).toString().trimmed();
    return isPinned(kind, id) ? unpin(kind, id) : pin(input);
}

bool HomeStore::movePin(const QString &kind, const QString &id, int toIndex)
{
    if (!m_pins.move(kind, id, toIndex)) {
        return false;
    }
    savePins();
    return true;
}

bool HomeStore::renamePin(const QString &kind, const QString &id, const QString &title, const QString &subtitle)
{
    if (!m_pins.rename(kind, id, title, subtitle)) {
        return false;
    }
    savePins();
    return true;
}

bool HomeStore::isPinned(const QString &kind, const QString &id) const
{
    return m_pins.contains(kind, id);
}

QString HomeStore::noteActivity(const QVariantMap &input)
{
    Home::ActivityEvent event = Home::ActivityEvent::fromVariant(input);
    event.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.occurredAt = QDateTime::currentMSecsSinceEpoch();
    event.unread = input.value(QStringLiteral("unread"), true).toBool();
    if (!m_activity.note(event)) {
        return {};
    }
    saveActivity();
    return event.id;
}

bool HomeStore::dismissActivity(const QString &eventId)
{
    if (!m_activity.dismiss(eventId)) {
        return false;
    }
    saveActivity();
    return true;
}

void HomeStore::markActivityRead()
{
    if (m_activity.markAllRead() > 0) {
        saveActivity();
    }
}

void HomeStore::clearActivity()
{
    if (m_activity.items().isEmpty()) {
        return;
    }
    m_activity.clear();
    saveActivity();
}

void HomeStore::forgetCollection(const QString &collectionId)
{
    if (m_pins.forgetCollection(collectionId) > 0) {
        savePins();
    }
    if (m_activity.forgetCollection(collectionId) > 0) {
        saveActivity();
    }
}

void HomeStore::savePins()
{
    m_settings->setValue(PinsKey, compactJson(m_pins.toVariant()));
    emit pinsChanged();
}

void HomeStore::saveActivity()
{
    m_settings->setValue(ActivityKey, compactJson(m_activity.toVariant()));
    emit activityChanged();
}
