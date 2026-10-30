.pragma library

// Where a Gravity Well's icons sit, in the Well's own coordinates.
//
// These are the logical slots: fixed hit boxes that pointer input is tested
// against. Anything visual (magnification, spreading apart, the trip into
// and out of the Well) is drawn on top as a transform and never moves a
// slot, so what is under the pointer doesn't shift while it moves.
//
// Only the linear form exists now. A vertical, arc or circular Well is one
// more branch here returning the same shape of result.

// form: "linear"; orientation: "horizontal" or "vertical".
// options: { slot, gap, padding, lead, dividerAt, dividerSize }
//   lead: room before the first icon (none now; kept for other forms).
//   dividerAt: index of the first icon after the divider, or -1 for none.
// Returns { slots: [{ x, y, size }], divider: { x, y } or null, width, height }.
function layout(form, orientation, count, options) {
    var slot = options.slot
    var gap = options.gap
    var padding = options.padding
    var dividerAt = options.dividerAt === undefined ? -1 : options.dividerAt
    var dividerSize = dividerAt > 0 && dividerAt < count ? options.dividerSize : 0
    var vertical = orientation === "vertical"

    var slots = []
    var along = padding + (options.lead || 0)
    var divider = null
    for (var index = 0; index < count; index += 1) {
        if (index === dividerAt && dividerSize > 0) {
            var middle = along - gap / 2 + dividerSize / 2
            divider = vertical ? { x: padding + slot / 2, y: middle } : { x: middle, y: padding + slot / 2 }
            along += dividerSize
        }
        slots.push(vertical
            ? { x: padding, y: along, size: slot }
            : { x: along, y: padding, size: slot })
        along += slot + (index < count - 1 ? gap : 0)
    }
    along += padding

    var across = slot + padding * 2
    return {
        slots: slots,
        divider: divider,
        width: vertical ? across : along,
        height: vertical ? along : across
    }
}

// Which slot a point (in Well coordinates) is over, or -1.
function slotAt(slots, x, y) {
    for (var index = 0; index < slots.length; index += 1) {
        var slot = slots[index]
        if (x >= slot.x && x < slot.x + slot.size && y >= slot.y && y < slot.y + slot.size) {
            return index
        }
    }
    return -1
}

// Where the Well's top left goes for a saved position: x and y are 0..1 of
// the room the Well can move in (0.5, 1 is bottom centre), so it stays put
// relative to the window as the window resizes, and always fully visible.
function place(position, wellWidth, wellHeight, areaWidth, areaHeight, margin, bottomClearance) {
    var roomX = Math.max(0, areaWidth - wellWidth - margin * 2)
    var roomY = Math.max(0, areaHeight - wellHeight - margin * 2 - bottomClearance)
    var px = Math.min(1, Math.max(0, Number(position.x)))
    var py = Math.min(1, Math.max(0, Number(position.y)))
    return { x: margin + px * roomX, y: margin + py * roomY }
}

// The inverse of place: a top left (where the Well was dropped) back to 0..1.
// Lands on the centre line when it comes within snap pixels of it.
function position(x, y, wellWidth, wellHeight, areaWidth, areaHeight, margin, bottomClearance, snap) {
    var roomX = Math.max(0, areaWidth - wellWidth - margin * 2)
    var roomY = Math.max(0, areaHeight - wellHeight - margin * 2 - bottomClearance)
    var px = roomX > 0 ? (x - margin) / roomX : 0.5
    var py = roomY > 0 ? (y - margin) / roomY : 1
    px = Math.min(1, Math.max(0, px))
    py = Math.min(1, Math.max(0, py))
    if (roomX > 0 && Math.abs((px - 0.5) * roomX) <= snap) {
        px = 0.5
    }
    if (roomY > 0 && (1 - py) * roomY <= snap) {
        py = 1
    }
    if (roomY > 0 && py * roomY <= snap) {
        py = 0
    }
    return { x: px, y: py }
}

// The Dock wave: for a pointer at `pointer` along the Well (Well
// coordinates; null when it isn't over the Well), how much each icon is
// drawn bigger and how far it steps aside. The icon nearest the pointer
// grows most, its neighbours less, and the rest stay as they are. Each
// icon pushes the ones after it along by half its growth and the ones
// before it back by the other half, so they never overlap and the one under
// the pointer stays under it. Slots are untouched: this is drawing only.
//
// options: { maxScale, reach, pitch, vertical }
//   reach: how many slots out the growth fades to nothing.
//   pitch: distance between neighbouring slot centres.
// Returns { scales, shifts, start, end, dividerShift }: start and end are
// where the drawn row begins and ends along the Well (it widens both ways).
function magnify(slots, pointer, options, divider) {
    var count = slots.length
    var vertical = options.vertical === true
    var scales = []
    var growth = []
    for (var index = 0; index < count; index += 1) {
        var slot = slots[index]
        var centre = (vertical ? slot.y : slot.x) + slot.size / 2
        var scale = 1
        if (pointer !== null && pointer !== undefined && options.maxScale > 1) {
            var distance = Math.abs(pointer - centre) / options.pitch
            if (distance < options.reach) {
                var fall = 0.5 * (1 + Math.cos(Math.PI * distance / options.reach))
                scale = 1 + (options.maxScale - 1) * fall
            }
        }
        scales.push(scale)
        growth.push(slot.size * (scale - 1))
    }

    var total = 0
    for (index = 0; index < count; index += 1) {
        total += growth[index]
    }
    var shifts = []
    var before = 0
    var start = Infinity
    var end = -Infinity
    for (index = 0; index < count; index += 1) {
        // Pushed forward by half of everything before, back by half of
        // everything after.
        var after = total - before - growth[index]
        var shift = before / 2 - after / 2
        shifts.push(shift)
        before += growth[index]
        slot = slots[index]
        var lead = (vertical ? slot.y : slot.x) + shift - growth[index] / 2
        start = Math.min(start, lead)
        end = Math.max(end, lead + slot.size * scales[index])
    }

    // The divider stays halfway between the icons either side of it.
    var dividerShift = 0
    if (divider && divider.index > 0 && divider.index < count) {
        var left = divider.index - 1
        var right = divider.index
        var leftEdge = (vertical ? slots[left].y : slots[left].x) + slots[left].size + shifts[left] + growth[left] / 2
        var rightEdge = (vertical ? slots[right].y : slots[right].x) + shifts[right] - growth[right] / 2
        var plainLeft = (vertical ? slots[left].y : slots[left].x) + slots[left].size
        var plainRight = vertical ? slots[right].y : slots[right].x
        dividerShift = (leftEdge + rightEdge) / 2 - (plainLeft + plainRight) / 2
    }

    return {
        scales: scales,
        shifts: shifts,
        start: count > 0 ? start : 0,
        end: count > 0 ? end : 0,
        dividerShift: dividerShift
    }
}

// How far a point is from a rectangle (0 inside it).
function distanceToRect(x, y, width, height) {
    var dx = Math.max(0, -x, x - width)
    var dy = Math.max(0, -y, y - height)
    return Math.sqrt(dx * dx + dy * dy)
}

// The Well's reveal, from the pointer's distance to it. It wakes (`near`)
// as the pointer comes within revealIn and sleeps again only past
// revealOut; it engages (magnifies, shows tooltips) within engageIn and
// lets go only past engageOut. The gaps between in and out are what keep
// it from flickering on the edge. `level` is 0..1: how much to show.
//
// previous: { near, engaged }; distance: null when the pointer is gone.
// options: { revealIn, revealOut, engageIn, engageOut }
function proximity(previous, distance, options) {
    if (distance === null || distance === undefined) {
        return { near: false, engaged: false, level: 0 }
    }
    var engaged = previous.engaged ? distance <= options.engageOut : distance <= options.engageIn
    var near = engaged || (previous.near ? distance <= options.revealOut : distance <= options.revealIn)
    var level = 0
    if (engaged) {
        level = 1
    } else if (near) {
        var span = Math.max(1, options.revealOut - options.engageIn)
        level = Math.min(1, Math.max(0, 1 - (distance - options.engageIn) / span))
        // Eases in: most of the change happens close up.
        level = level * level * (3 - 2 * level)
    }
    return { near: near, engaged: engaged, level: level }
}
