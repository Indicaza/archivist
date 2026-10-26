.pragma library

var entries = ({})
var insertionOrder = []
var maximumEntries = 256

function getBlocks(key, content) {
    var cacheKey = String(key || "")
    var text = String(content || "")
    var entry = entries[cacheKey]

    if (!entry || entry.content !== text) {
        return null
    }

    return entry.blocks
}

function putBlocks(key, content, blocks) {
    var cacheKey = String(key || "")

    if (cacheKey.length === 0) {
        return
    }

    if (!entries[cacheKey]) {
        insertionOrder.push(cacheKey)
    }

    entries[cacheKey] = {
        content: String(content || ""),
        blocks: blocks
    }

    while (insertionOrder.length > maximumEntries) {
        var oldestKey = insertionOrder.shift()

        if (oldestKey !== cacheKey) {
            delete entries[oldestKey]
        }
    }
}
