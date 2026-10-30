.pragma library

// The Gravity Well's hover card: what a sphere holds, where the card sits,
// and when it shows. Plain functions over what the Well already has (its
// Docking.wells() row and a few lookups the workspace passes in), so the
// card never opens, wakes or reads anything to find out more.

// How long the pointer rests on a sphere before the card shows; once it's
// showing, the gap between spheres it forgives; and how long after it
// closes the next sphere shows its card at once.
var openDelay = 300
var closeGrace = 120
var warmFor = 600

function text(value) {
    return value === undefined || value === null ? "" : String(value)
}

// What an entry holds, for the card.
//
// `look` turns a view's info ({ viewId, typeId, title, resource }) into
// what the card shows: title(info), icon(info), kind(info) (its type, or
// "" when that isn't known), path(info) and dirty(info).
//
// Returns { anchor, title, icon, kind, path, dirty, pinned, count, note,
// rows }.
// A panel put away (an Anchor) lists its tabs (Magnets) in tab order, each
// { viewId, title, icon, kind, active, dirty, missing }; anything else is
// one Magnet and has no rows.
function describe(entry, look) {
    var item = entry || ({})
    var views = item.views || []
    var state = text(item.state)
    var out = {
        anchor: item.panel === true,
        title: "",
        icon: "grid",
        kind: "",
        path: "",
        dirty: false,
        pinned: item.pinned === true,
        state: state,
        count: views.length,
        note: "",
        rows: []
    }

    if (out.anchor) {
        out.title = text(item.anchorName) || text(item.title) || "Untitled"
        var active = text(item.activeViewId)
        for (var index = 0; index < views.length; index += 1) {
            var view = views[index] || ({})
            if (view.missing === true) {
                out.rows.push({
                    viewId: text(view.viewId),
                    title: "Unavailable",
                    icon: "file",
                    kind: "",
                    active: false,
                    dirty: false,
                    missing: true
                })
                continue
            }
            var info = infoOf(view)
            var dirty = look.dirty(info) === true
            out.dirty = out.dirty || dirty
            out.rows.push({
                viewId: info.viewId,
                title: text(look.title(info)) || "Untitled",
                icon: text(look.icon(info)) || "file",
                kind: text(look.kind(info)),
                active: info.viewId === active,
                dirty: dirty,
                missing: false
            })
        }
        out.note = views.length === 1 ? "1 Magnet" : views.length + " Magnets"
        return out
    }

    // One resource: the view put away last, else (pinned, not open) what
    // the entry remembers of it.
    var latest = null
    for (var at = views.length - 1; at >= 0 && latest === null; at -= 1) {
        if (views[at] && views[at].missing !== true) {
            latest = views[at]
        }
    }
    var single = latest !== null ? infoOf(latest) : {
        viewId: text(item.entryId),
        typeId: text(item.typeId),
        title: text(item.title),
        resource: item.resource || ({})
    }
    out.title = text(look.title(single)) || "Untitled"
    out.icon = text(look.icon(single)) || "file"
    out.kind = text(look.kind(single))
    out.path = text(look.path(single))
    out.dirty = look.dirty(single) === true
    out.note = views.length > 1 ? views.length + " views put away"
        : state === "pinnedOpen" ? "Pinned · open"
        : state === "pinnedClosed" ? "Pinned · not open"
        : ""
    return out
}

function infoOf(view) {
    return {
        viewId: text(view.viewId),
        typeId: text(view.typeId),
        title: text(view.title),
        resource: view.resource || ({})
    }
}

// How many rows fit: never taller than `available`, at most `cap` rows.
// When some don't fit, one row says how many more there are.
// Returns { shown, more }.
function fit(total, available, header, row, cap) {
    var count = Math.max(0, Math.floor(total))
    var room = Math.max(1, Math.floor((available - header) / Math.max(1, row)))
    var limit = Math.max(1, Math.min(Math.floor(cap), room))
    if (count <= limit) {
        return { shown: count, more: 0 }
    }
    var shown = Math.max(1, limit - 1)
    return { shown: shown, more: count - shown }
}

// Where the card sits beside a sphere, inside `bounds` (all in one
// coordinate space). `sphere` is { x, y, width, height } as drawn; the card
// goes on `prefer`'s side ("above", "below", "left" or "right") unless it
// only fits on the other, and slides along the edge to stay inside.
// Returns { x, y, side, pointer } (pointer: where along the card's edge the
// sphere is).
function place(sphere, width, height, bounds, prefer, gap, margin) {
    var opposite = { above: "below", below: "above", left: "right", right: "left" }
    var room = {
        above: sphere.y - gap - bounds.y - margin,
        below: bounds.y + bounds.height - margin - (sphere.y + sphere.height + gap),
        left: sphere.x - gap - bounds.x - margin,
        right: bounds.x + bounds.width - margin - (sphere.x + sphere.width + gap)
    }
    var side = opposite[prefer] ? prefer : "above"
    var across = side === "above" || side === "below"
    var need = across ? height : width
    if (room[side] < need && room[opposite[side]] > room[side]) {
        side = opposite[side]
    }

    var x
    var y
    if (across) {
        x = clamp(sphere.x + sphere.width / 2 - width / 2, bounds.x + margin, bounds.x + bounds.width - margin - width)
        y = side === "above" ? sphere.y - gap - height : sphere.y + sphere.height + gap
    } else {
        y = clamp(sphere.y + sphere.height / 2 - height / 2, bounds.y + margin, bounds.y + bounds.height - margin - height)
        x = side === "left" ? sphere.x - gap - width : sphere.x + sphere.width + gap
    }
    var pointer = across ? sphere.x + sphere.width / 2 - x : sphere.y + sphere.height / 2 - y
    var length = across ? width : height
    return { x: x, y: y, side: side, pointer: clamp(pointer, Math.min(12, length / 2), Math.max(length / 2, length - 12)) }
}

function clamp(value, low, high) {
    return high < low ? low : Math.max(low, Math.min(high, value))
}

// When the card shows. One small machine the Well feeds: "enter" (the
// pointer is over a sphere: `entryId`), "leave" (over none), "timer" (the
// timer it was asked to run fired) and "cancel" (a press, a drag, a move
// or a menu: away at once). Returns the next state ({ phase, entryId }),
// with `timer` ("open", "close", "warm"; "stop"; or null to leave it be)
// and `visible`. Moving from sphere to sphere while the card shows only
// changes what it shows; it never closes and opens again.
function hover(state, event) {
    var phase = state && state.phase ? state.phase : "idle"
    var entryId = state ? text(state.entryId) : ""
    var type = event ? event.type : ""
    var next = { phase: phase, entryId: entryId, timer: null }

    if (type === "cancel") {
        next = { phase: "idle", entryId: "", timer: "stop" }
    } else if (type === "enter") {
        var target = text(event.entryId)
        if (phase === "idle") {
            next = { phase: "waiting", entryId: target, timer: "open" }
        } else if (phase === "waiting") {
            next = { phase: "waiting", entryId: target, timer: null }
        } else {
            // Showing, closing, or just closed: show this one at once.
            next = { phase: "shown", entryId: target, timer: "stop" }
        }
    } else if (type === "leave") {
        if (phase === "waiting") {
            next = { phase: "idle", entryId: "", timer: "stop" }
        } else if (phase === "shown") {
            next = { phase: "closing", entryId: entryId, timer: "close" }
        }
    } else if (type === "timer") {
        if (phase === "waiting") {
            next = { phase: "shown", entryId: entryId, timer: "stop" }
        } else if (phase === "closing") {
            next = { phase: "warm", entryId: "", timer: "warm" }
        } else if (phase === "warm") {
            next = { phase: "idle", entryId: "", timer: "stop" }
        }
    }
    next.visible = next.phase === "shown" || next.phase === "closing"
    return next
}

function delayFor(timer) {
    return timer === "open" ? openDelay : timer === "close" ? closeGrace : warmFor
}
