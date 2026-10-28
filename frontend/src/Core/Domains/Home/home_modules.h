#pragma once

#include <QList>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// What Home shows under the composer, and in what order.
//
// Home is a personal layer above every project. It isn't a workspace: it has
// no split tree and no Anchors, so it doesn't need the docking engine. It is
// an ordered list of modules, each a compact presentation of something that
// lives elsewhere (recent work, pins, activity, a saved layout, later a
// plugin's resource).
//
// Two things are kept apart:
//   ModuleDescriptor  a kind of module: what it is, what it may read, and the
//                     workspace view type that shows the same thing in full.
//                     Built-in and (later) plugin modules register the same
//                     way, like ViewTypeDescriptor does for views.
//   ModuleEntry       one module placed on this person's Home: shown or not,
//                     how wide, its own small settings.
namespace Home
{
inline constexpr int kModuleContractVersion = 1;

namespace ModuleTypes
{
inline const QString Resume = QStringLiteral("archivist.home.resume");
inline const QString Pinned = QStringLiteral("archivist.home.pinned");
inline const QString Activity = QStringLiteral("archivist.home.activity");
inline const QString Layouts = QStringLiteral("archivist.home.layouts");
} // namespace ModuleTypes

namespace Spans
{
inline const QString Full = QStringLiteral("full");
inline const QString Half = QStringLiteral("half");
} // namespace Spans

struct ModuleDescriptor
{
    QString typeId; // reverse-DNS: archivist.home.resume, plugin.acme.weather
    int contractVersion = kModuleContractVersion;
    QString title;
    QString description;
    QString icon;
    QString defaultSpan = Spans::Full;
    bool builtIn = false;
    // The resource kind this module presents, and the workspace view type
    // that shows that resource in full. A plugin supplies one resource and
    // gets both presentations: compact on Home, full in an Anchor.
    QString resourceKind;
    QString expandViewType;
    // What the module reads, in words the person approves before it is
    // shown ("calendar.read", "network.weather"). Built-ins read only what
    // Archivist already has, so theirs is empty. Nothing is collected in
    // the background for a module that isn't on Home.
    QStringList permissions;

    [[nodiscard]] QVariantMap toVariant() const;
};

class ModuleRegistry
{
public:
    // Resume work, Pinned, Activity and Layouts.
    static ModuleRegistry withBuiltIns();

    // Refuses malformed or duplicate ids and other contract versions.
    // `error` (optional) gets a sentence saying why.
    bool registerType(const ModuleDescriptor &descriptor, QString *error = nullptr);
    bool unregisterType(const QString &typeId);

    [[nodiscard]] bool contains(const QString &typeId) const { return m_types.contains(typeId); }
    [[nodiscard]] const ModuleDescriptor *find(const QString &typeId) const;
    [[nodiscard]] QList<ModuleDescriptor> types() const;

    [[nodiscard]] static bool isValidTypeId(const QString &typeId);

private:
    QHash<QString, ModuleDescriptor> m_types;
    QStringList m_order;
};

struct ModuleEntry
{
    QString id;     // stable per Home; the type id for built-ins
    QString typeId;
    bool visible = true;
    QString span = Spans::Full;
    QVariantMap settings; // small scalar values only

    bool operator==(const ModuleEntry &other) const;
    bool operator!=(const ModuleEntry &other) const { return !(*this == other); }
    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static ModuleEntry fromVariant(const QVariantMap &value);
};

namespace Presets
{
inline const QString Balanced = QStringLiteral("balanced");
inline const QString Focused = QStringLiteral("focused");
inline const QString Overview = QStringLiteral("overview");
inline const QString Custom = QStringLiteral("custom");
} // namespace Presets

struct PresetInfo
{
    QString id;
    QString title;
    QString description;
};

// The predefined arrangements, default first.
const QList<PresetInfo> &presets();

class HomeLayout
{
public:
    static constexpr int Version = 1;

    // A predefined arrangement. Unknown ids give the default.
    static HomeLayout preset(const QString &presetId, const ModuleRegistry &registry);
    static HomeLayout defaults(const ModuleRegistry &registry) { return preset(Presets::Balanced, registry); }

    // Forgiving: drops malformed and duplicate entries, keeps modules whose
    // type isn't registered (a plugin that's missing today may be back
    // tomorrow) and adds registered built-ins it doesn't mention, hidden.
    // Anything unreadable gives the default.
    static HomeLayout fromVariant(const QVariantMap &value, const ModuleRegistry &registry);
    [[nodiscard]] QVariantMap toVariant() const;

    [[nodiscard]] const QList<ModuleEntry> &modules() const { return m_modules; }
    [[nodiscard]] int indexOf(const QString &moduleId) const;
    [[nodiscard]] const ModuleEntry *find(const QString &moduleId) const;

    // Each returns false and changes nothing when there's nothing to do.
    bool move(const QString &moduleId, int toIndex);
    bool setVisible(const QString &moduleId, bool visible);
    bool setSpan(const QString &moduleId, const QString &span);
    // Places a module of a registered type that isn't on Home yet, shown,
    // at the end. Returns its id, or empty.
    QString add(const QString &typeId, const ModuleRegistry &registry);

    // The preset this layout is exactly, or Presets::Custom.
    [[nodiscard]] QString matchingPreset(const ModuleRegistry &registry) const;

    bool operator==(const HomeLayout &other) const { return m_modules == other.m_modules; }
    bool operator!=(const HomeLayout &other) const { return !(*this == other); }

private:
    QList<ModuleEntry> m_modules;
};
} // namespace Home
