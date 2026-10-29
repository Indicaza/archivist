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
//   lead: room before the first icon (the Well's grip).
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
