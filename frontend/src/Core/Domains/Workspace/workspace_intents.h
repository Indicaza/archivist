#pragma once

#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_model.h"

#include <QHash>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The workspace in the words people use: "open these files beside our
// conversation", "make the code bigger", "put the docs on the right",
// "keep this panel where it is".
//
// A small set of semantic actions on top of WorkspaceModel, for agent tools
// and anything else that is not dragging panels. Each action resolves vague
// destinations ("the editor", "the right", "this panel") against the anchors
// that exist, reuses views that already show a resource, and then runs the
// ordinary model operations with the caller's Origin, so layout and content
// locks apply exactly as they do everywhere else.
//
// A request runs against a working copy. The model only changes when every
// step succeeded, so a request that fails halfway leaves nothing behind.
//
// Requests ({action, ...}):
//
//   describe
//   show     items: [{typeId, resource, title} | {viewId} | {surface: "chat"|"terminal"}],
//            where ("" = choose; "right", "beside the chat", an area name, "new"),
//            purpose ("" or e.g. "documentation", "reference", "code"),
//            focus (bool)
//   focus    target
//   resize   target, change ("larger", "smaller", "maximize", "restore", "equal", or 0.1..0.9)
//   move     target, to ("left", "right", "top", "bottom", "beside <area>")
//   keep     target                 (layout lock: it stays where it is)
//   label    target, name           (what the person calls it)
//
// Targets and areas are anchor ids or descriptions: a name, what an area
// shows ("the code", "the chat", "the documentation"), where it is ("the
// right panel", "the bottom"), or "this" for the focused one.
namespace Workspace::Intents
{
struct Environment
{
    const ViewTypeRegistry *registry = nullptr;
    // Size of each surface in pixels, for "the right panel" and resizing.
    // Missing surfaces use a typical window size.
    QHash<QString, QSizeF> surfaceSizes;
    LayoutMetrics metrics;
    Origin origin = Origin::Agent;
    // Keyboard focus may move (the person asked for it and isn't typing).
    bool mayMoveFocus = false;
    // Anchors the person focused, most recent first.
    QStringList recentAnchors;
    // Where the conversation lives when no anchor shows a chat: the bottom
    // Chat/Terminal workbench. Empty when there is none.
    QString commandSurfaceName = QStringLiteral("the bottom panel");
    // Share of its split a side panel opened for reference material gets.
    double sidePanelShare = 0.34;
    // Smallest share "smaller" leaves, and the largest "larger" reaches.
    double minimumShare = 0.15;
    double maximumShare = 0.82;
};

struct Outcome
{
    bool ok = true;
    QString code;    // NOT_FOUND, LAYOUT_LOCKED, CONTENT_LOCKED, UNSUPPORTED, INVALID_ARGUMENT, ...
    QString message; // for the person
    bool changed = false;
    QStringList opened;   // views created
    QStringList revealed; // existing views brought forward or moved
    QStringList createdAnchors;
    QString focusAnchorId; // set when focus moved on purpose
    // Asks the shell to bring up the bottom workbench: "chat" or "terminal".
    QString commandSurface;
    QStringList notes; // what was done, in plain words
    QVariantMap data;  // describe's payload

    static Outcome failure(const QString &code, const QString &message)
    {
        Outcome outcome;
        outcome.ok = false;
        outcome.code = code;
        outcome.message = message;
        return outcome;
    }
};

struct AreaMatch
{
    QString anchorId;
    QString viewId; // when the description named one tab ("workspace_model.cpp")
    int score = 0;

    [[nodiscard]] bool isValid() const { return !anchorId.isEmpty(); }
};

[[nodiscard]] QVariantMap describe(const WorkspaceModel &model, const Environment &environment);
Outcome perform(WorkspaceModel &model, const QVariantMap &request, const Environment &environment);
[[nodiscard]] AreaMatch resolveArea(const WorkspaceModel &model, const QString &description, const Environment &environment);
// "Editor (left, 62%)": how an area is named back to the person.
[[nodiscard]] QString areaLabel(const WorkspaceModel &model, const QString &anchorId);
[[nodiscard]] QStringList actions();
} // namespace Workspace::Intents
