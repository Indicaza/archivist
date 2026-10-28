#include "Core/Domains/Home/home_modules.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace Home
{
namespace
{
constexpr int MaxModules = 24;
constexpr int MaxSettings = 12;
constexpr int MaxTextLength = 200;

QString normalizedSpan(const QString &span, const QString &fallback = Spans::Full)
{
    const QString value = span.trimmed().toLower();
    if (value == Spans::Full || value == Spans::Half) {
        return value;
    }
    return fallback;
}

QVariantMap scalarSettings(const QVariantMap &settings)
{
    QVariantMap result;
    for (auto it = settings.cbegin(); it != settings.cend() && result.size() < MaxSettings; ++it) {
        switch (static_cast<QMetaType::Type>(it.value().typeId())) {
        case QMetaType::QString:
            result.insert(it.key(), it.value().toString().left(MaxTextLength));
            break;
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
            result.insert(it.key(), it.value());
            break;
        default:
            break;
        }
    }
    return result;
}

ModuleEntry entryFor(const ModuleDescriptor &descriptor, bool visible, const QString &span = {})
{
    ModuleEntry entry;
    entry.id = descriptor.typeId;
    entry.typeId = descriptor.typeId;
    entry.visible = visible;
    entry.span = normalizedSpan(span, descriptor.defaultSpan);
    return entry;
}

struct PresetSlot
{
    QString typeId;
    bool visible;
    QString span;
};

QList<PresetSlot> presetSlots(const QString &presetId)
{
    using namespace ModuleTypes;
    if (presetId == Presets::Focused) {
        return {
            {Resume, true, Spans::Full},
            {Pinned, false, Spans::Half},
            {Activity, false, Spans::Half},
            {Layouts, false, Spans::Full},
        };
    }
    if (presetId == Presets::Overview) {
        return {
            {Resume, true, Spans::Full},
            {Activity, true, Spans::Half},
            {Pinned, true, Spans::Half},
            {Layouts, true, Spans::Full},
        };
    }
    return {
        {Resume, true, Spans::Full},
        {Pinned, true, Spans::Half},
        {Activity, true, Spans::Half},
        {Layouts, false, Spans::Full},
    };
}
} // namespace

QVariantMap ModuleDescriptor::toVariant() const
{
    return {
        {QStringLiteral("typeId"), typeId},
        {QStringLiteral("contractVersion"), contractVersion},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), description},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("defaultSpan"), defaultSpan},
        {QStringLiteral("builtIn"), builtIn},
        {QStringLiteral("resourceKind"), resourceKind},
        {QStringLiteral("expandViewType"), expandViewType},
        {QStringLiteral("permissions"), permissions},
    };
}

ModuleRegistry ModuleRegistry::withBuiltIns()
{
    ModuleRegistry registry;

    ModuleDescriptor resume;
    resume.typeId = ModuleTypes::Resume;
    resume.title = QStringLiteral("Resume work");
    resume.description = QStringLiteral("Projects you worked in lately, opened where you left them.");
    resume.icon = QStringLiteral("history");
    resume.builtIn = true;
    resume.resourceKind = QStringLiteral("collection");
    registry.registerType(resume);

    ModuleDescriptor pinned;
    pinned.typeId = ModuleTypes::Pinned;
    pinned.title = QStringLiteral("Pinned");
    pinned.description = QStringLiteral("Conversations, projects, files and layouts you keep close.");
    pinned.icon = QStringLiteral("pin");
    pinned.defaultSpan = Spans::Half;
    pinned.builtIn = true;
    registry.registerType(pinned);

    ModuleDescriptor activity;
    activity.typeId = ModuleTypes::Activity;
    activity.title = QStringLiteral("Activity");
    activity.description = QStringLiteral("Replies that finished and work that needs you.");
    activity.icon = QStringLiteral("bell");
    activity.defaultSpan = Spans::Half;
    activity.builtIn = true;
    registry.registerType(activity);

    ModuleDescriptor layouts;
    layouts.typeId = ModuleTypes::Layouts;
    layouts.title = QStringLiteral("Layouts");
    layouts.description = QStringLiteral("Your saved workspace layouts, one click from Home.");
    layouts.icon = QStringLiteral("grid");
    layouts.builtIn = true;
    layouts.resourceKind = QStringLiteral("layout");
    registry.registerType(layouts);

    return registry;
}

bool ModuleRegistry::isValidTypeId(const QString &typeId)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"));
    return typeId.size() <= 120 && pattern.match(typeId).hasMatch();
}

bool ModuleRegistry::registerType(const ModuleDescriptor &descriptor, QString *error)
{
    auto refuse = [error](const QString &reason) {
        if (error) {
            *error = reason;
        }
        return false;
    };
    if (!isValidTypeId(descriptor.typeId)) {
        return refuse(QStringLiteral("A Home module needs an id like vendor.name."));
    }
    if (descriptor.contractVersion != kModuleContractVersion) {
        return refuse(QStringLiteral("This module was built for another version of Archivist's Home."));
    }
    if (m_types.contains(descriptor.typeId)) {
        return refuse(QStringLiteral("A Home module with this id is already registered."));
    }
    if (descriptor.title.trimmed().isEmpty()) {
        return refuse(QStringLiteral("A Home module needs a title."));
    }
    ModuleDescriptor stored = descriptor;
    stored.defaultSpan = normalizedSpan(descriptor.defaultSpan);
    m_types.insert(stored.typeId, stored);
    m_order.append(stored.typeId);
    return true;
}

bool ModuleRegistry::unregisterType(const QString &typeId)
{
    if (!m_types.remove(typeId)) {
        return false;
    }
    m_order.removeAll(typeId);
    return true;
}

const ModuleDescriptor *ModuleRegistry::find(const QString &typeId) const
{
    auto it = m_types.constFind(typeId);
    return it == m_types.cend() ? nullptr : &it.value();
}

QList<ModuleDescriptor> ModuleRegistry::types() const
{
    QList<ModuleDescriptor> result;
    for (const QString &typeId : m_order) {
        result.append(m_types.value(typeId));
    }
    return result;
}

bool ModuleEntry::operator==(const ModuleEntry &other) const
{
    return id == other.id && typeId == other.typeId && visible == other.visible && span == other.span
        && settings == other.settings;
}

QVariantMap ModuleEntry::toVariant() const
{
    QVariantMap value{
        {QStringLiteral("id"), id},
        {QStringLiteral("typeId"), typeId},
        {QStringLiteral("visible"), visible},
        {QStringLiteral("span"), span},
    };
    if (!settings.isEmpty()) {
        value.insert(QStringLiteral("settings"), settings);
    }
    return value;
}

ModuleEntry ModuleEntry::fromVariant(const QVariantMap &value)
{
    ModuleEntry entry;
    entry.typeId = value.value(QStringLiteral("typeId")).toString().trimmed();
    entry.id = value.value(QStringLiteral("id"), entry.typeId).toString().trimmed();
    entry.visible = value.value(QStringLiteral("visible"), true).toBool();
    entry.span = normalizedSpan(value.value(QStringLiteral("span")).toString());
    entry.settings = scalarSettings(value.value(QStringLiteral("settings")).toMap());
    return entry;
}

const QList<PresetInfo> &presets()
{
    static const QList<PresetInfo> list{
        {Presets::Balanced, QStringLiteral("Balanced"), QStringLiteral("Recent work, pins and activity under the composer.")},
        {Presets::Focused, QStringLiteral("Focused"), QStringLiteral("Just the conversation and where you left off.")},
        {Presets::Overview, QStringLiteral("Overview"), QStringLiteral("Everything at a glance, layouts included.")},
    };
    return list;
}

HomeLayout HomeLayout::preset(const QString &presetId, const ModuleRegistry &registry)
{
    HomeLayout layout;
    QSet<QString> placed;
    for (const PresetSlot &slot : presetSlots(presetId)) {
        if (const ModuleDescriptor *descriptor = registry.find(slot.typeId)) {
            layout.m_modules.append(entryFor(*descriptor, slot.visible, slot.span));
            placed.insert(slot.typeId);
        }
    }
    // Other registered modules are available but not shown.
    for (const ModuleDescriptor &descriptor : registry.types()) {
        if (!placed.contains(descriptor.typeId)) {
            layout.m_modules.append(entryFor(descriptor, false));
        }
    }
    return layout;
}

HomeLayout HomeLayout::fromVariant(const QVariantMap &value, const ModuleRegistry &registry)
{
    const QVariant rawModules = value.value(QStringLiteral("modules"));
    if (rawModules.typeId() != QMetaType::QVariantList || value.value(QStringLiteral("version")).toInt() > Version) {
        return defaults(registry);
    }

    HomeLayout layout;
    QSet<QString> ids;
    QSet<QString> types;
    for (const QVariant &raw : rawModules.toList()) {
        if (layout.m_modules.size() >= MaxModules) {
            break;
        }
        ModuleEntry entry = ModuleEntry::fromVariant(raw.toMap());
        if (entry.id.isEmpty() || !ModuleRegistry::isValidTypeId(entry.typeId) || ids.contains(entry.id)) {
            continue;
        }
        ids.insert(entry.id);
        types.insert(entry.typeId);
        layout.m_modules.append(entry);
    }
    if (layout.m_modules.isEmpty()) {
        return defaults(registry);
    }
    for (const ModuleDescriptor &descriptor : registry.types()) {
        if (descriptor.builtIn && !types.contains(descriptor.typeId) && !ids.contains(descriptor.typeId)) {
            layout.m_modules.append(entryFor(descriptor, false));
        }
    }
    return layout;
}

QVariantMap HomeLayout::toVariant() const
{
    QVariantList modules;
    for (const ModuleEntry &entry : m_modules) {
        modules.append(entry.toVariant());
    }
    return {
        {QStringLiteral("version"), Version},
        {QStringLiteral("modules"), modules},
    };
}

int HomeLayout::indexOf(const QString &moduleId) const
{
    for (int index = 0; index < m_modules.size(); ++index) {
        if (m_modules.at(index).id == moduleId) {
            return index;
        }
    }
    return -1;
}

const ModuleEntry *HomeLayout::find(const QString &moduleId) const
{
    const int index = indexOf(moduleId);
    return index < 0 ? nullptr : &m_modules.at(index);
}

bool HomeLayout::move(const QString &moduleId, int toIndex)
{
    const int from = indexOf(moduleId);
    if (from < 0) {
        return false;
    }
    const int to = std::clamp(toIndex, 0, static_cast<int>(m_modules.size()) - 1);
    if (to == from) {
        return false;
    }
    m_modules.move(from, to);
    return true;
}

bool HomeLayout::setVisible(const QString &moduleId, bool visible)
{
    const int index = indexOf(moduleId);
    if (index < 0 || m_modules.at(index).visible == visible) {
        return false;
    }
    m_modules[index].visible = visible;
    return true;
}

bool HomeLayout::setSpan(const QString &moduleId, const QString &span)
{
    const int index = indexOf(moduleId);
    const QString value = normalizedSpan(span, QString());
    if (index < 0 || value.isEmpty() || m_modules.at(index).span == value) {
        return false;
    }
    m_modules[index].span = value;
    return true;
}

QString HomeLayout::add(const QString &typeId, const ModuleRegistry &registry)
{
    const ModuleDescriptor *descriptor = registry.find(typeId);
    if (!descriptor || m_modules.size() >= MaxModules) {
        return {};
    }
    for (ModuleEntry &entry : m_modules) {
        if (entry.typeId == typeId) {
            if (entry.visible) {
                return {};
            }
            entry.visible = true;
            return entry.id;
        }
    }
    m_modules.append(entryFor(*descriptor, true));
    return typeId;
}

QString HomeLayout::matchingPreset(const ModuleRegistry &registry) const
{
    for (const PresetInfo &info : presets()) {
        if (preset(info.id, registry) == *this) {
            return info.id;
        }
    }
    return Presets::Custom;
}
} // namespace Home
