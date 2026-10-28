#pragma once

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
//   strategy  "" | columns | rows | focus_with_sidebar   (used without a template)
//   slots     [{name, holds: ["code", "docs", ...], weight, items}]
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
inline constexpr int kMaximumSlots = 6;
inline constexpr int kMaximumItemsPerSlot = 8;
inline constexpr int kMaximumItems = 16;

struct Template
{
    QString id;
    QString name;
    int version = 1;
    QString description;
    QJsonObject recipe; // {tree, slots}; see workspace_compose.cpp
};

[[nodiscard]] QList<Template> templates();
[[nodiscard]] QStringList templateIds();
[[nodiscard]] QStringList strategies();
// The words a slot's holds may use ("code", "docs", "chat", ...).
[[nodiscard]] QStringList holdWords();

struct Slot
{
    QString name;
    QStringList holds;
    double weight = 0;   // 0: default
    QVariantList items;  // Intents show items: {typeId, resource, title} | {viewId} | {surface}
};

struct Spec
{
    QString templateId;
    QString strategy;
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
