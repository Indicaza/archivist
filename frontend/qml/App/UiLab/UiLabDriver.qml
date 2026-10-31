import QtQuick
import Archivist.Services 1.0

// Runs one Observatory UI Lab scenario inside the app and reports what each
// action cost. Loaded by App.qml only in a disposable test profile.
//
// Each step is dispatched (synthetic mouse input through the window, the
// QML function a button calls, or the store call a control makes), then
// timed three ways, all on UiLab's monotonic clock:
//   dispatchMs  the call or input returned
//   stateMs     the step's expected UI state was first seen (polled ~1 ms)
//   settledMs   the last frame the window swapped before rendering went
//               quiet (settleQuietMs without a frame); null if no frame
// plus the frames swapped in between (count, longest interval). The step's
// method is recorded so input-driven steps are never confused with calls.
//
// While a step runs, UiLab tags every operation the app reports (LiveEvents
// spans and markers) and every event-loop stall with the step's index; the
// whole list is written once, when the run finishes ("operations").
Item {
    id: root

    readonly property int driverVersion: 1
    property var window: null
    property var shell: null
    property var scenario: UiLab ? UiLab.scenario : ({})
    property var plan: []
    property int cursor: 0
    property var keys: ({})
    property var actions: []
    property var report: ({})
    property double runStart: 0
    property string stage: "idle"
    property var current: null
    property bool finished: false

    readonly property int actionTimeoutMs: Number(scenario.actionTimeoutMs || 5000)
    readonly property int settleQuietMs: Number(scenario.settleQuietMs || 120)
    readonly property int timeoutMs: Number(scenario.timeoutMs || 180000)
    readonly property int readyTimeoutMs: Number(scenario.readyTimeoutMs || 30000)

    function start(win, shellItem) {
        window = win
        shell = shellItem
        UiLab.attachWindow(win)
        runStart = UiLab.nowMs()
        plan = buildPlan(scenario)
        report = {
            schema: "archivist.ui-lab.events",
            schemaVersion: 1,
            driverVersion: driverVersion,
            scenario: { id: String(scenario.id || ""), version: Number(scenario.version || 0) },
            status: "running",
            plannedActions: plan.length,
            clock: { source: "LiveEvents::nowNs", runStartMs: runStart },
            readyMs: null,
            isolation: null,
            error: null,
            actions: []
        }
        UiLab.log("scenario " + report.scenario.id + " v" + report.scenario.version + ", " + plan.length + " actions")
        save()
        stage = "ready"
        ticker.start()
    }

    // setup, warm-up and measured iterations of `steps`, teardown.
    function buildPlan(definition) {
        var list = []
        var add = function(phase, iteration, steps) {
            for (var index = 0; index < (steps || []).length; index += 1) {
                list.push({ phase: phase, iteration: iteration, stepIndex: index, step: steps[index] })
            }
        }
        add("setup", 0, definition.setup)
        var warmup = Number(definition.warmupIterations || 0)
        var iterations = Number(definition.iterations || 1)
        for (var w = 0; w < warmup; w += 1) {
            add("warmup", w + 1, definition.steps)
        }
        for (var i = 0; i < iterations; i += 1) {
            add("measured", i + 1, definition.steps)
        }
        add("teardown", 0, definition.teardown)
        return list
    }

    function save() {
        report.actions = actions
        UiLab.writeResult(report)
    }

    function finishRun(status, error) {
        if (finished) {
            return
        }
        finished = true
        ticker.stop()
        report.status = status
        report.error = error || null
        report.totalMs = UiLab.nowMs() - runStart
        UiLab.setAction(-1)
        report.operations = UiLab.operations()
        save()
        UiLab.log("finished: " + status + (error ? " (" + error + ")" : ""))
        UiLab.finish(status === "passed" ? 0 : status === "failed" ? 1 : 2)
    }

    function workspace() {
        return shell ? shell.workspaceItem : null
    }

    function ready() {
        var ws = workspace()
        return window && window.visible && ws && typeof ws.minimizeView === "function"
            && String(Docking.primarySurfaceId || "").length > 0
            && UiLab.lastFrameMs() >= 0
    }

    // The id of a seeded fixture chat ("big" -> its chat id), or "".
    function seededChat(key) {
        var chats = UiLab.seedStatus().chats || ({})
        var id = chats[String(key)]
        return id === undefined ? "" : String(id)
    }

    // A resource whose id is "seed:<key>" points at a seeded chat.
    function resolveResource(resource) {
        var copy = {}
        for (var field in (resource || {})) {
            copy[field] = resource[field]
        }
        if (typeof copy.id === "string" && copy.id.indexOf("seed:") === 0) {
            copy.id = seededChat(copy.id.slice(5))
        }
        return copy
    }

    function viewId(key) {
        var id = keys[String(key)]
        return id === undefined ? "" : String(id)
    }

    function wellEntryFor(id) {
        var wells = Docking.wells()
        for (var w = 0; w < wells.length; w += 1) {
            var entries = wells[w].entries || []
            for (var e = 0; e < entries.length; e += 1) {
                var minimized = entries[e].minimizedViewIds || []
                for (var m = 0; m < minimized.length; m += 1) {
                    if (String(minimized[m]) === id) {
                        return String(entries[e].entryId)
                    }
                }
            }
        }
        return ""
    }

    function sceneCenter(item) {
        return item.mapToItem(null, item.width / 2, item.height / 2)
    }

    function findShown(objectName) {
        var item = UiLab.findItem(String(objectName))
        if (!item) {
            return { error: "no item named " + objectName }
        }
        if (!item.visible || item.width <= 0 || item.height <= 0) {
            return { error: objectName + " is not shown" }
        }
        return { item: item }
    }

    // Performs a step. Returns { ok, method, error }.
    function dispatch(step) {
        var ws = workspace()
        var id = ""
        switch (String(step.op)) {
        case "click": {
            var found = findShown(step.objectName)
            if (!found.item) {
                return { ok: false, method: "input", error: found.error }
            }
            var point = sceneCenter(found.item)
            UiLab.mouse("press", point.x, point.y)
            UiLab.mouse("release", point.x, point.y)
            return { ok: true, method: "input" }
        }
        case "dragSidebarEdge": {
            // Drags the side panel's edge so the panel is `width` wide.
            var handle = findShown("shell.explorerResizeHandle")
            if (!handle.item) {
                return { ok: false, method: "input", error: handle.error }
            }
            var from = sceneCenter(handle.item)
            var target = shell.mapToItem(null, shell.theme.activityRailWidth + Number(step.width), 0)
            var moves = Math.max(1, Number(step.moves || 8))
            UiLab.mouse("press", from.x, from.y)
            for (var n = 1; n <= moves; n += 1) {
                UiLab.mouse("move", from.x + (target.x - from.x) * n / moves, from.y)
            }
            UiLab.mouse("release", target.x, from.y)
            return { ok: true, method: "input" }
        }
        case "returnToWorkspace":
            Shell.returnToWorkspace()
            return { ok: true, method: "store-call" }
        case "setPanelOpen":
            Shell.setPanelOpen(step.open === true)
            return { ok: true, method: "store-call" }
        case "seedChats":
            // Deterministic fixture chats in the test profile's database.
            return UiLab.seedChats({ seed: Number(step.seed || 1), chats: step.chats || [] })
                ? { ok: true, method: "fixture" }
                : { ok: false, method: "fixture", error: String(UiLab.seedStatus().error || "seeding refused") }
        case "refreshChats":
            ChatStore.refresh()
            return { ok: true, method: "store-call" }
        case "selectChat":
            // What picking a conversation in the side panel does.
            id = seededChat(step.key)
            if (!id) {
                return { ok: false, method: "store-call", error: "no seeded chat " + step.key }
            }
            ChatStore.selectChat(id)
            return { ok: true, method: "store-call" }
        case "openView":
            id = String(Docking.openView(String(step.typeId), resolveResource(step.resource), String(step.title || ""), {}))
            if (id.length === 0) {
                return { ok: false, method: "store-call", error: "openView returned no view" }
            }
            keys[String(step.key)] = id
            return { ok: true, method: "store-call" }
        case "applyLayout":
            return Docking.applyLayout(String(step.layoutId))
                ? { ok: true, method: "store-call" }
                : { ok: false, method: "store-call", error: "applyLayout refused " + step.layoutId }
        case "activateView":
            id = viewId(step.key)
            return Docking.activateView(id)
                ? { ok: true, method: "store-call" }
                : { ok: false, method: "store-call", error: "activateView refused " + step.key }
        case "minimizeView":
            // The Gravity Well path a tab's minimize button takes (Genie included).
            id = viewId(step.key)
            if (!id) {
                return { ok: false, method: "ui-function", error: "unknown view " + step.key }
            }
            ws.minimizeView(id)
            return { ok: true, method: "ui-function" }
        case "restoreView": {
            // What clicking the view's sphere in the Well does.
            id = viewId(step.key)
            var entryId = wellEntryFor(id)
            if (!entryId) {
                return { ok: false, method: "ui-function", error: step.key + " is not in a Well" }
            }
            ws.restoreFromWell(entryId)
            return { ok: true, method: "ui-function" }
        }
        case "noop":
            return { ok: true, method: "none" }
        default:
            return { ok: false, method: "none", error: "unknown op " + step.op }
        }
    }

    // Whether every expectation of the step holds. Returns "" when met,
    // otherwise what is still unmet.
    function unmet(expect) {
        var ws = workspace()
        for (var name in expect) {
            var want = expect[name]
            var ok = true
            switch (name) {
            case "panelOpen":
                ok = Shell.panelOpen === want
                break
            case "panelId":
                ok = String(Shell.panelId) === String(want)
                break
            case "homeActive":
                ok = Shell.homeActive === want
                break
            case "sidebarSettled":
                ok = Math.abs(shell.explorerExtent - (Shell.panelOpen ? shell.clampedExplorerWidth : 0)) < 0.5
                break
            case "explorerWidth":
                ok = Math.abs(shell.explorerWidth - Number(want)) < 1
                break
            case "currentLayoutId":
                ok = String(Docking.currentLayoutId) === String(want)
                break
            case "focusedView":
                ok = String(Docking.focusedViewId) === viewId(want)
                break
            case "minimized":
                for (var key in want) {
                    ok = ok && viewId(key).length > 0 && Docking.isViewMinimized(viewId(key)) === want[key]
                }
                break
            case "viewsOpen":
                for (var v = 0; v < want.length; v += 1) {
                    var row = Docking.view(viewId(want[v]))
                    ok = ok && row.viewId !== undefined && row.minimized === false
                }
                break
            case "genieIdle":
                ok = !ws || !ws.genieActive
                break
            case "seeded":
                var seed = UiLab.seedStatus()
                if (seed.state === "failed") {
                    return "unknown expectation: seeding failed: " + seed.error
                }
                ok = (seed.state === "done") === (want === true)
                break
            case "chatsListed": {
                var listed = {}
                var all = ChatStore.chats || []
                for (var c = 0; c < all.length; c += 1) {
                    listed[String(all[c].id)] = true
                }
                for (var k = 0; k < want.length; k += 1) {
                    ok = ok && listed[seededChat(want[k])] === true
                }
                break
            }
            case "chatShown":
                // The conversation is selected and its first page of messages
                // is in the store (the frames that follow are the settle time).
                ok = ChatStore.selectedChatId === seededChat(want.key)
                    && !ChatStore.loadingMessages
                    && (ChatStore.messages || []).length >= Number(want.minMessages || 1)
                break
            default:
                return "unknown expectation " + name
            }
            if (!ok) {
                return name
            }
        }
        return ""
    }

    function beginAction(entry) {
        var step = entry.step
        UiLab.setAction(cursor)
        var t0 = UiLab.nowMs()
        var outcome = dispatch(step)
        var t1 = UiLab.nowMs()
        current = {
            index: cursor,
            phase: entry.phase,
            iteration: entry.iteration,
            step: entry.stepIndex,
            op: String(step.op),
            label: step.label ? String(step.label) : null,
            method: outcome.method,
            t0: t0,
            dispatchMs: t1 - t0,
            stateAt: -1,
            ok: outcome.ok,
            error: outcome.error || null
        }
        if (!outcome.ok) {
            completeAction(null, false)
            return
        }
        stage = "state"
        checkState()
    }

    function checkState() {
        var missing = unmet(plan[cursor].step.expect || {})
        if (missing.indexOf("unknown expectation") === 0) {
            current.ok = false
            current.error = missing
            completeAction(null, false)
            return
        }
        var now = UiLab.nowMs()
        if (missing === "") {
            current.stateAt = now
            stage = "settle"
        } else if (now - current.t0 > actionTimeoutMs) {
            current.ok = false
            current.timedOut = true
            current.error = "timed out waiting for " + missing
            completeAction(null, false)
        }
    }

    function checkSettled() {
        var now = UiLab.nowMs()
        var last = UiLab.lastFrameMs()
        var quietSince = Math.max(last, current.stateAt)
        if (now - quietSince >= settleQuietMs) {
            completeAction(last > current.t0 ? last : null, false)
        } else if (now - current.t0 > actionTimeoutMs) {
            // Rendering never went quiet (something keeps animating).
            completeAction(null, true)
        }
    }

    function completeAction(settledAt, settleTimedOut) {
        var end = UiLab.nowMs()
        UiLab.setAction(-1)
        var record = {
            index: current.index,
            phase: current.phase,
            iteration: current.iteration,
            step: current.step,
            op: current.op,
            label: current.label,
            method: current.method,
            ok: current.ok,
            error: current.error,
            timedOut: current.timedOut === true,
            startMs: current.t0 - runStart,
            endMs: end - runStart,
            dispatchMs: current.dispatchMs,
            stateMs: current.stateAt >= 0 ? current.stateAt - current.t0 : null,
            settledMs: settledAt !== null ? Math.max(settledAt, current.stateAt) - current.t0 : null,
            settleTimedOut: settleTimedOut,
            frames: UiLab.frames(current.t0, end)
        }
        var list = actions
        list.push(record)
        actions = list
        save()
        cursor += 1
        current = null
        if (!record.ok && scenario.stopOnFailure !== false) {
            finishRun("failed", "action " + record.index + " (" + record.op + "): " + record.error)
            return
        }
        stage = "next"
    }

    function tick() {
        if (finished) {
            return
        }
        var now = UiLab.nowMs()
        if (now - runStart > timeoutMs) {
            finishRun("timeout", "scenario exceeded " + timeoutMs + " ms")
            return
        }
        switch (stage) {
        case "ready":
            if (ready()) {
                report.readyMs = now - runStart
                report.isolation = UiLab.isolation()
                if (!report.isolation.ok) {
                    finishRun("error", "storage is not isolated in the test profile")
                    return
                }
                stage = "next"
            } else if (now - runStart > readyTimeoutMs) {
                finishRun("timeout", "the app was not ready after " + readyTimeoutMs + " ms")
            }
            break
        case "next":
            if (cursor >= plan.length) {
                var failures = actions.filter(function(action) { return !action.ok }).length
                finishRun(failures === 0 ? "passed" : "failed", failures ? failures + " action(s) failed" : "")
            } else {
                beginAction(plan[cursor])
            }
            break
        case "state":
            checkState()
            break
        case "settle":
            checkSettled()
            break
        }
    }

    Timer {
        id: ticker

        interval: 1
        repeat: true
        onTriggered: root.tick()
    }
}
