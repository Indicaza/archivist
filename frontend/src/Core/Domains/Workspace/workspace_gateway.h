#pragma once

#include <QVariantMap>

#include <functional>

// How code that doesn't own the workspace reaches it: agent tools run on the
// AI thread, while the workspace lives with the UI.
//
// The app installs one handler (WorkspaceAgentBridge), which carries each
// request over to the UI thread and waits for the answer. Without a handler
// (tests, a headless runtime) calls fail politely with code UNAVAILABLE.
//
// A request is a workspace intent (see workspace_intents.h) plus who asks:
// {action, ..., runId, chatId, explicit}. `explicit` means the person's own
// message asked for a workspace change in this turn.
namespace Workspace::Gateway
{
using Handler = std::function<QVariantMap(const QVariantMap &request)>;

// Replaces the handler; an empty one uninstalls it. Thread safe.
void install(Handler handler);
[[nodiscard]] bool installed();
// Thread safe. Returns the handler's answer, or {ok: false, code, error}.
QVariantMap call(const QVariantMap &request);
} // namespace Workspace::Gateway
