.pragma library

// The smallest set of changes that turns a panel's tab strip (`current`, a
// list of view ids) into `wanted`: removals, then inserts and moves in
// order. A tab that stays is never removed and made again, so putting one
// tab in the Well, or bringing one back, touches only that tab.
//
// Returns [{ op: "remove", at }, { op: "insert", at, id }, { op: "move", from, to }],
// to be applied one after another.
function changes(current, wanted) {
    var ops = []
    var strip = current.slice()
    for (var index = strip.length - 1; index >= 0; index -= 1) {
        if (wanted.indexOf(strip[index]) < 0) {
            ops.push({ op: "remove", at: index })
            strip.splice(index, 1)
        }
    }
    for (var at = 0; at < wanted.length; at += 1) {
        var found = strip.indexOf(wanted[at], at)
        if (found < 0) {
            ops.push({ op: "insert", at: at, id: wanted[at] })
            strip.splice(at, 0, wanted[at])
        } else if (found !== at) {
            ops.push({ op: "move", from: found, to: at })
            strip.splice(at, 0, strip.splice(found, 1)[0])
        }
    }
    return ops
}
