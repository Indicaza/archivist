#pragma once

#include "Core/Domains/Workspace/workspace_model.h"

#include <QString>
#include <QStringList>

// Where new content goes when nobody says exactly where.
//
// One set of rules for every caller: the Explorer, the command layer and
// agent tools. The rules only read the model; callers open the view.
namespace Workspace::Placement
{
struct Hints
{
    Origin origin = Origin::User;
    // Anchors the person focused, most recent first (session only).
    QStringList recentAnchors;
};

// An existing view of the resource in this type, preferring one in
// `preferredAnchorId`. Empty when none: the caller opens a new one.
[[nodiscard]] QString existingView(
    const WorkspaceModel &model,
    const QString &typeId,
    const ResourceRef &resource,
    const QString &preferredAnchorId = {}
);

// The anchor a new view of `typeId` should open in. Never empty while the
// model has an anchor.
[[nodiscard]] QString chooseAnchor(
    const WorkspaceModel &model,
    const QString &typeId,
    const ResourceRef &resource = {},
    const Hints &hints = {}
);

// True when `origin` may add a view to the anchor.
[[nodiscard]] bool canReceive(const WorkspaceModel &model, const QString &anchorId, Origin origin);
} // namespace Workspace::Placement
