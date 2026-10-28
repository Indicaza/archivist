import QtQuick

// A ListModel that follows a JS array by key with moves, inserts and
// removes instead of resets, so views can animate the change (a pin moved,
// an event arrived). Rows hold only `key`; delegates read the item with
// `itemFor(key)`.
ListModel {
    id: root

    property var items: []
    // item => unique string
    property var keyOf: function(item) { return String(item.id) }
    property var byKey: ({})

    function itemFor(key) {
        return byKey[String(key)] || ({})
    }

    function sync() {
        var next = items || []
        var keys = []
        var lookup = {}
        for (var index = 0; index < next.length; index += 1) {
            var key = String(keyOf(next[index]))
            if (lookup[key] !== undefined) {
                continue
            }
            lookup[key] = next[index]
            keys.push(key)
        }
        byKey = lookup

        // Remove rows that are gone.
        for (var row = count - 1; row >= 0; row -= 1) {
            if (lookup[get(row).key] === undefined) {
                remove(row)
            }
        }
        // Move or insert into place.
        for (var target = 0; target < keys.length; target += 1) {
            var found = -1
            for (var scan = target; scan < count; scan += 1) {
                if (get(scan).key === keys[target]) {
                    found = scan
                    break
                }
            }
            if (found < 0) {
                insert(target, { key: keys[target], revision: 0 })
            } else if (found !== target) {
                move(found, target, 1)
            }
        }
        // Tell delegates their item may have changed.
        for (var refresh = 0; refresh < count; refresh += 1) {
            setProperty(refresh, "revision", get(refresh).revision + 1)
        }
    }

    onItemsChanged: sync()
    Component.onCompleted: sync()
}
