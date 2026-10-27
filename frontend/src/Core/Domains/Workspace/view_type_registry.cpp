#include "Core/Domains/Workspace/view_type_registry.h"

#include <QRegularExpression>

namespace Workspace
{
namespace
{
ViewTypeDescriptor builtIn(
    const QString &typeId,
    const QString &displayName,
    const QString &icon,
    const QString &resourceKind,
    bool worktreeBound
)
{
    ViewTypeDescriptor descriptor;
    descriptor.typeId = typeId;
    descriptor.displayName = displayName;
    descriptor.icon = icon;
    descriptor.resourceKind = resourceKind;
    descriptor.worktreeBound = worktreeBound;
    return descriptor;
}
} // namespace

ViewTypeRegistry ViewTypeRegistry::withBuiltIns()
{
    ViewTypeRegistry registry;
    registry.registerType(builtIn(ViewTypes::Chat, QStringLiteral("Chat"), QStringLiteral("chat"), QStringLiteral("chat"), false));
    registry.registerType(builtIn(ViewTypes::Editor, QStringLiteral("Editor"), QStringLiteral("code"), QStringLiteral("document"), true));
    registry.registerType(builtIn(ViewTypes::Terminal, QStringLiteral("Terminal"), QStringLiteral("terminal"), QStringLiteral("terminal"), true));
    registry.registerType(builtIn(ViewTypes::Preview, QStringLiteral("Preview"), QStringLiteral("file"), QStringLiteral("file"), true));
    registry.registerType(builtIn(ViewTypes::Diff, QStringLiteral("Diff"), QStringLiteral("diff"), QStringLiteral("diff"), true));

    ViewTypeDescriptor welcome = builtIn(ViewTypes::Welcome, QStringLiteral("Welcome"), QStringLiteral("home"), QString(), false);
    welcome.allowsDuplicates = false;
    registry.registerType(welcome);
    return registry;
}

bool ViewTypeRegistry::isValidTypeId(const QString &typeId)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"));
    return typeId.size() <= 128 && pattern.match(typeId).hasMatch();
}

OpResult ViewTypeRegistry::registerType(const ViewTypeDescriptor &descriptor)
{
    if (!isValidTypeId(descriptor.typeId)) {
        return OpResult::failure(QStringLiteral("INVALID_TYPE_ID"), QStringLiteral("View type ids look like vendor.name: ") + descriptor.typeId);
    }

    if (descriptor.contractVersion != kViewContractVersion) {
        return OpResult::failure(
            QStringLiteral("CONTRACT_VERSION"),
            QStringLiteral("%1 targets view contract %2; this Archivist speaks %3.")
                .arg(descriptor.typeId)
                .arg(descriptor.contractVersion)
                .arg(kViewContractVersion)
        );
    }

    if (descriptor.stateVersion < 1) {
        return OpResult::failure(QStringLiteral("STATE_VERSION"), QStringLiteral("State versions start at 1."));
    }

    if (m_types.contains(descriptor.typeId)) {
        return OpResult::failure(QStringLiteral("DUPLICATE_TYPE"), descriptor.typeId + QStringLiteral(" is already registered."));
    }

    m_types.insert(descriptor.typeId, descriptor);
    m_order.append(descriptor.typeId);
    return OpResult::success(descriptor.typeId);
}

bool ViewTypeRegistry::unregisterType(const QString &typeId)
{
    m_order.removeAll(typeId);
    return m_types.remove(typeId) > 0;
}

bool ViewTypeRegistry::contains(const QString &typeId) const
{
    return m_types.contains(typeId);
}

const ViewTypeDescriptor *ViewTypeRegistry::find(const QString &typeId) const
{
    const auto it = m_types.constFind(typeId);
    return it == m_types.constEnd() ? nullptr : &it.value();
}

QList<ViewTypeDescriptor> ViewTypeRegistry::types() const
{
    QList<ViewTypeDescriptor> result;
    result.reserve(m_order.size());

    for (const QString &typeId : m_order) {
        result.append(m_types.value(typeId));
    }

    return result;
}

ViewRecord ViewTypeRegistry::migrate(const ViewRecord &view, bool *migrated) const
{
    if (migrated) {
        *migrated = false;
    }

    const ViewTypeDescriptor *descriptor = find(view.typeId);

    if (!descriptor || view.stateVersion >= descriptor->stateVersion) {
        return view;
    }

    ViewRecord upgraded = view;

    if (descriptor->migrateState) {
        upgraded.state = descriptor->migrateState(view.state, view.stateVersion);
    }

    upgraded.stateVersion = descriptor->stateVersion;

    if (migrated) {
        *migrated = true;
    }

    return upgraded;
}
} // namespace Workspace
