#pragma once

#include "Core/Domains/Workspace/workspace_compose_catalog.h"
#include "Core/Domains/Workspace/workspace_model.h"

#include <QJsonObject>
#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Describing a whole workspace at once.
//
// Instead of a series of "split, move, resize" steps, the caller (usually an
// agent) says what the workspace should look like: a few named slots, what
// each holds and how much room it gets, arranged by a strategy or a
// template. compose turns that into an ordinary layout document plus the
// content of each slot, which the Intents "compose" action applies to the
// model in one transaction (one publish, one undo step).
//
//   template  "" | focus_references | chat_code | code_diff | research_desk | agent_overview
//   strategy  "" | grid | columns | rows | split | stack   (used without a template)
//   rows, columns   the grid's size (0: from the panel count)
//   slots     [{name, holds: ["code", "docs", ...], weight, position, items}]
//
// The caller never builds a split tree: it picks a primitive and names a
// position for each panel ("bottom-left"), and the tree, the sizes and the
// fallbacks come from here.
//   focus     a slot name to bring forward, or ""
//
// With a template, slots fill the template's panels by name (or by what
// they hold); slots the template doesn't have go in a column at the side.
// Weights are relative (0 keeps the default; 2 is about twice as much
// room, but no panel gets more than kMaximumWeightRatio times a sibling,
// so "dominant" stays usable: about 64% beside a column).
// Native code solves the geometry for the real viewport: every panel keeps
// at least kMinimumPanelWidth x kMinimumPanelHeight, and when that can't
// fit, the least important neighbours share one panel as tabs instead of
// being squeezed. Panels that exist and already
// show a slot's content are reused, so their editors and terminals are not
// rebuilt and the change animates instead of popping.
//
// Specifications are checked before anything changes. Limits keep them
// small: at most kMaximumSlots slots and kMaximumItems items in all.
namespace Workspace::Compose
{
struct Slot
{
    QString name;
    QStringList holds;
    double weight = 0;   // 0: default
    // Where it goes: "top-left", "bottom-right", "r2c1" in a grid; "left",
    // "right" in columns; "top", "bottom" in rows; "primary" (or the side
    // of the rest) in a split. "": the next free place.
    QString position;
    QVariantList items;  // Intents show items: {typeId, resource, title} | {viewId} | {surface}
    bool placeholder = false; // made up to fill a grid; a reused panel keeps its own name
};

struct Spec
{
    QString templateId;
    QString strategy;    // the primitive: grid, columns, rows, split, stack
    int rows = 0;        // grid only; 0: from the panel count
    int columns = 0;
    QList<Slot> panels;
    QString focus;
    // The main window's panel area in pixels (after the sidebar and the
    // bottom panel). Invalid: a typical 1440 x 900.
    QSizeF viewport;
};

// The smallest panel that is still usable, in pixels.
inline constexpr double kMinimumPanelWidth = 300;
inline constexpr double kMinimumPanelHeight = 180;
// No panel gets more than this many times the room of a sibling.
inline constexpr double kMaximumWeightRatio = 1.8;

struct Plan
{
    bool ok = false;
    QString code;    // INVALID_ARGUMENT when the spec is unusable
    QString message;
    QVariantMap request; // the Intents compose request
    QStringList slotNames;
    // What the solver had to change to fit ("“Skull” is a tab in “Resume”").
    QStringList adjustments;
};

// Checks the spec and builds the request against the current model (for
// reusing panels). Nothing changes here.
[[nodiscard]] Plan plan(const Spec &spec, const WorkspaceModel &model);
} // namespace Workspace::Compose
