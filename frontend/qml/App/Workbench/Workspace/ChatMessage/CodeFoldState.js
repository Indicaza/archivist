.pragma library

// Which folded code blocks the reader expanded, by message and block, so a
// block keeps its state when its row scrolls away and comes back.
var expanded = ({})
var count = 0

function isExpanded(key) {
    return key.length > 0 && expanded[key] === true
}

function setExpanded(key, value) {
    if (key.length === 0) {
        return
    }

    if (value) {
        if (expanded[key] !== true) {
            count += 1
        }

        expanded[key] = true
    } else if (expanded[key] === true) {
        delete expanded[key]
        count -= 1
    }

    // Bounded: a long session forgets the oldest choices.
    if (count > 400) {
        expanded = ({})
        count = 0
    }
}
