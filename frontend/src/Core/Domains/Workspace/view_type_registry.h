#pragma once

#include "Core/Domains/Workspace/workspace_types.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <functional>

// The internal view contract. Everything the docking engine needs to know
// about a kind of content lives here, so built-in surfaces and, later, plugin
// views register the same way and the engine never special-cases a type.
//
// Versioning:
//   kViewContractVersion  shape of this descriptor. A descriptor built for a
//                         different major contract version is refused.
//   stateVersion          shape of one type's saved ViewRecord::state. When a
//                         saved view is older, migrateState upgrades it; a
//                         newer one is kept as is and flagged, never dropped.
namespace Workspace
{
inline constexpr int kViewContractVersion = 1;

// How a live view instance may move between parents.
enum class TransferPolicy
{
    // Reparent the live item inside its window; rebuild from saved view
    // state when it crosses into another native window. Safe for anything
    // whose canonical state lives in a native store.
    ReparentInWindow,
    // Always rebuild from saved view state. For content that cannot be
    // reparented at all.
    AlwaysRebuild,
};

struct ViewTypeDescriptor
{
    QString typeId;  // reverse-DNS style: archivist.editor, plugin.acme.graph
    int contractVersion = kViewContractVersion;
    int stateVersion = 1;
    QString displayName;
    QString icon;
    // Resource kind this view shows (document, chat, terminal, ...). Empty
    // for views that need no resource.
    QString resourceKind;
    // Several views may show the same resource (a split of one document).
    bool allowsDuplicates = true;
    // The resource belongs to a worktree (Library). Such views keep their
    // own worktree instead of following focus.
    bool worktreeBound = false;
    TransferPolicy transfer = TransferPolicy::ReparentInWindow;
    // Upgrades saved view state from `fromVersion` to `stateVersion`.
    std::function<QVariantMap(const QVariantMap &state, int fromVersion)> migrateState;
};

class ViewTypeRegistry
{
public:
    // The built-in Archivist view types.
    static ViewTypeRegistry withBuiltIns();

    // Refuses empty or malformed ids, duplicates and descriptors for another
    // contract version.
    OpResult registerType(const ViewTypeDescriptor &descriptor);
    bool unregisterType(const QString &typeId);

    [[nodiscard]] bool contains(const QString &typeId) const;
    [[nodiscard]] const ViewTypeDescriptor *find(const QString &typeId) const;
    [[nodiscard]] QList<ViewTypeDescriptor> types() const;

    // Brings a saved view up to its type's current state version. Unknown
    // types and newer-than-known states come back unchanged; `*migrated`
    // reports whether anything changed.
    ViewRecord migrate(const ViewRecord &view, bool *migrated = nullptr) const;

    [[nodiscard]] static bool isValidTypeId(const QString &typeId);

private:
    QHash<QString, ViewTypeDescriptor> m_types;
    QStringList m_order;
};

// Built-in type ids.
namespace ViewTypes
{
inline const QString Chat = QStringLiteral("archivist.chat");
inline const QString Editor = QStringLiteral("archivist.editor");
inline const QString Terminal = QStringLiteral("archivist.terminal");
inline const QString Preview = QStringLiteral("archivist.preview");
inline const QString Diff = QStringLiteral("archivist.diff");
inline const QString Welcome = QStringLiteral("archivist.welcome");
} // namespace ViewTypes
} // namespace Workspace
