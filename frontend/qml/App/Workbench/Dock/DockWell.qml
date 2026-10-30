import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"
import "WellGeometry.js" as WellGeometry

// A Gravity Well: a few small spheres for what's been minimized or pinned,
// floating over the layout like a Dock. It shows references the workspace
// holds (Docking.wells()); clicking one restores, brings forward or reopens
// it (WorkspaceStore). It's an accessory over the layout, never a panel.
//
// At rest it all but disappears, leaving a faint locator. As the pointer
// comes near, the spheres fade in and lift; over them, the one nearest the
// pointer swells and its neighbours step aside (the Dock wave). Pointer
// input is tested against fixed slots (WellGeometry), so the wave never
// moves what's under the pointer. Drag the space around the spheres to move
// the Well; it's saved with the layout. Nothing here writes to the model
// while the pointer moves.
Item {
    id: root

    objectName: "dock.well"

    required property var theme
    required property var workspace
    // One row of Docking.wells().
    required property var well
    // A tab or panel is being dragged, and over this Well: letting go puts
    // it away here.
    readonly property bool dragging: Docking.dragActive
    readonly property bool dropHere: dragging && Docking.dragWellId === wellId

    readonly property string wellId: String(well.wellId || "")
    readonly property var entries: {
        var all = well.entries || []
        var pinned = []
        var loose = []
        for (var index = 0; index < all.length; index += 1) {
            (all[index].pinned ? pinned : loose).push(all[index])
        }
        return pinned.concat(loose)
    }
    readonly property int pinnedCount: {
        var count = 0
        for (var index = 0; index < entries.length; index += 1) {
            if (entries[index].pinned) {
                count += 1
            }
        }
        return count
    }
    readonly property bool empty: entries.length === 0
    readonly property bool vertical: String(well.orientation || "horizontal") === "vertical"

    readonly property real margin: theme.px(12)
    readonly property int dividerAt: pinnedCount > 0 && pinnedCount < entries.length ? pinnedCount : -1
    readonly property var geometry: WellGeometry.layout(
        String(well.form || "linear"),
        String(well.orientation || "horizontal"),
        Math.max(1, entries.length),
        {
            slot: theme.px(40),
            gap: theme.px(6),
            padding: theme.px(10),
            lead: 0,
            dividerAt: dividerAt,
            dividerSize: theme.px(11)
        }
    )

    // ---- Where the pointer is, and how awake that makes the Well ----

    // How far out the Well notices the pointer, and the in/out distances
    // that keep it from flickering on the edge (WellGeometry.proximity).
    readonly property real reach: theme.px(120)
    readonly property var proximityOptions: ({
        revealIn: theme.px(96),
        revealOut: theme.px(118),
        engageIn: 0,
        engageOut: theme.px(22)
    })
    property var proximity: ({ near: false, engaged: false, level: 0 })
    // The pointer in the Well's coordinates, while it's near.
    property real pointerX: 0
    property real pointerY: 0
    property bool pointerHere: false

    function track() {
        var position = null
        if (wellHover.hovered) {
            position = wellHover.point.position
        } else if (fieldHover.hovered) {
            var inField = fieldHover.point.position
            position = Qt.point(inField.x + field.x, inField.y + field.y)
        }
        if (position === null) {
            pointerHere = false
            proximity = WellGeometry.proximity(proximity, null, proximityOptions)
            return
        }
        pointerHere = true
        pointerX = position.x
        pointerY = position.y
        proximity = WellGeometry.proximity(proximity, WellGeometry.distanceToRect(position.x, position.y, width, height), proximityOptions)
    }

    // Shown in full whatever the pointer does: while something is dragged
    // toward it, while it moves, while its menu is open, and for a moment
    // after something lands in it.
    readonly property bool held: dragging || moving || entryMenu.visible || wellMenu.visible || landing.running
        || root.workspace.genieActive === true
    readonly property bool engaged: proximity.engaged && !dragging
    readonly property real level: held ? 1 : proximity.level
    // How visible it is, eased.
    property real shown: theme.wellDormantOpacity + (1 - theme.wellDormantOpacity) * level
    Behavior on shown {
        NumberAnimation {
            duration: root.level > 0.5 ? root.theme.wellRevealDuration : root.theme.wellSettleDuration
            easing.type: Easing.OutCubic
        }
    }

    // The Dock wave, from where the pointer is along the Well. Only while
    // engaged; with reduced motion there's none.
    readonly property var wave: WellGeometry.magnify(
        geometry.slots,
        engaged && !moving && pointerHere && !theme.reducedMotion ? (vertical ? pointerY : pointerX) : null,
        {
            maxScale: theme.wellMagnification,
            reach: theme.wellMagnificationReach,
            pitch: theme.px(46),
            vertical: vertical
        },
        { index: dividerAt }
    )
    // A little smaller and lower while it's waking, bigger while something
    // is held over it.
    readonly property real revealScale: theme.reducedMotion ? 1 : dropHere ? 1.1 : 0.9 + 0.1 * level
    readonly property real lift: theme.reducedMotion ? 0 : theme.px(3) * level

    // Something just landed: show it for a moment.
    property int lastCount: 0
    onEntriesChanged: {
        if (entries.length > lastCount) {
            landing.restart()
        }
        lastCount = entries.length
    }
    Timer {
        id: landing

        interval: 1400
    }

    // ---- Where it sits ----

    // Reactive (the default): it sits in the free workspace and moves with
    // the layout (`reactiveFrame`: what the sidebar, the chat dock and the
    // composer leave). Fixed: it sits in the whole window (`windowFrame`)
    // and stays put whatever moves. Both in the parent's coordinates.
    readonly property bool fixed: well.fixed === true
    property rect reactiveFrame: Qt.rect(0, 0, parent ? parent.width : 0, parent ? parent.height : 0)
    property rect windowFrame: Qt.rect(0, 0, parent ? parent.width : 0, parent ? parent.height : 0)
    readonly property rect frame: fixed ? windowFrame : reactiveFrame

    // While it's being moved: where the pointer has it. Otherwise: where it
    // was saved, worked out again for its frame as it is now.
    property bool moving: false
    property real moveX: 0
    property real moveY: 0
    readonly property var rest: {
        var at = WellGeometry.place(
            { x: Number(well.x), y: Number(well.y) },
            width, height, frame.width, frame.height, margin, 0
        )
        return { x: frame.x + at.x, y: frame.y + at.y }
    }
    // In the top half, it grows downward, away from the window edge.
    readonly property bool growDown: !vertical && parent !== null && y + height / 2 < parent.height / 2

    // Its saved place for a top left in the parent, in a given frame.
    function shareIn(box, x, y, snap) {
        return WellGeometry.position(x - box.x, y - box.y, width, height, box.width, box.height, margin, 0, snap)
    }

    // Reactive or fixed, staying exactly where it is on screen.
    function setFixed(fixed) {
        var box = fixed ? windowFrame : reactiveFrame
        var share = shareIn(box, x, y, 0)
        Docking.setWellFixed(wellId, fixed, share.x, share.y)
    }

    // Where something dropped here lands, in `item`'s coordinates: the end
    // of the row, where its orb appears.
    function landingRect(item) {
        var size = theme.px(40)
        var corner = mapToItem(item, Math.max(0, width - size - theme.px(10)), Math.max(0, (height - size) / 2))
        return Qt.rect(corner.x, corner.y, size, size)
    }

    // Something floating over the layout that it keeps clear of (the
    // composer), in its parent's coordinates. In the way, it rises above
    // it, easing; out of the way again, it settles back.
    property rect obstacle: Qt.rect(0, 0, 0, 0)
    readonly property real wantedClearance: {
        if (fixed || vertical || obstacle.width <= 0 || obstacle.height <= 0) {
            return 0
        }
        var gap = theme.px(10)
        var across = rest.x < obstacle.x + obstacle.width && rest.x + width > obstacle.x
        var down = rest.y + height > obstacle.y - gap && rest.y < obstacle.y + obstacle.height
        return across && down ? Math.max(0, rest.y + height - (obstacle.y - gap)) : 0
    }
    property real clearance: wantedClearance
    Behavior on clearance {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
    }

    // Following the layout (a sidebar opening, the window resizing) it
    // moves with it, frame by frame; only a drop settles with a spring.
    property bool settling: false
    Timer {
        id: settleTimer

        interval: root.theme.wellSettleDuration * 2
        onTriggered: root.settling = false
    }

    // Dragged past the edge of its frame it gives a little, like a
    // scrolled list at its end, and springs back when let go.
    function rubber(value, low, high) {
        if (value < low) {
            return low - Math.min(theme.px(36), (low - value) * 0.3)
        }
        if (value > high) {
            return high + Math.min(theme.px(36), (value - high) * 0.3)
        }
        return value
    }

    x: moving ? moveX : rest.x
    y: moving ? moveY : Math.max(frame.y + margin, rest.y - clearance)
    width: geometry.width
    height: geometry.height
    z: moving ? 2 : 0

    // Picked up: it lifts a touch (bigger, a deeper shadow) while it moves.
    property real pickup: moving ? 1 : 0
    Behavior on pickup {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: root.theme.wellRevealDuration; easing.type: Easing.OutCubic }
    }
    scale: 1 + pickup * 0.04

    // Settles onto the centre line or an edge after a drop.
    Behavior on x {
        enabled: root.settling && !root.moving && !root.theme.reducedMotion
        SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 }
    }
    Behavior on y {
        enabled: root.settling && !root.moving && !root.theme.reducedMotion
        SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 }
    }

    Accessible.role: Accessible.ToolBar
    Accessible.name: empty ? "Gravity Well, empty" : "Gravity Well, " + entries.length + (entries.length === 1 ? " item" : " items")

    // The pointer over the Well itself (its spheres included).
    HoverHandler {
        id: wellHover

        onHoveredChanged: root.track()
        onPointChanged: root.track()
    }

    // The pointer near the Well: a wide, invisible ring that only watches.
    // It takes no clicks and doesn't keep hover from what's underneath.
    Item {
        id: field

        objectName: "dock.well.field"

        x: -root.reach
        y: -root.reach
        width: root.width + root.reach * 2
        height: root.height + root.reach * 2
        z: -2

        HoverHandler {
            id: fieldHover

            onHoveredChanged: root.track()
            onPointChanged: root.track()
        }
    }

    // The field the spheres float in: a soft pool of light under them that
    // widens with the wave. No edges, no bar.
    WellGlow {
        id: pool

        readonly property real along: root.wave.end - root.wave.start
        width: Math.max(root.theme.px(60), along + root.theme.px(36))
        height: width
        x: root.vertical ? (root.width - width) / 2 : root.wave.start + along / 2 - width / 2
        y: (root.growDown ? root.height * 0.38 : root.height * 0.62) - height / 2
        z: -1
        color: root.dropHere ? root.theme.accent : "#000000"
        strength: root.dropHere ? 0.34 : root.dragging ? 0.42 : 0.38
        opacity: root.empty && !root.dragging ? 0 : root.level
        transform: Scale {
            origin.x: pool.width / 2
            origin.y: pool.height / 2
            xScale: root.vertical ? (root.width + root.theme.px(36)) / pool.width : 1
            yScale: root.vertical ? 1 : (root.height + root.theme.px(26)) / pool.height
        }

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionHover } }
    }

    // At rest: a faint mark where the Well is, so it can be found.
    Rectangle {
        objectName: "dock.well.locator"

        anchors.horizontalCenter: parent.horizontalCenter
        y: root.growDown ? root.theme.px(4) : parent.height - height - root.theme.px(4)
        width: root.theme.px(18)
        height: root.theme.px(3)
        radius: height / 2
        color: root.theme.mutedText
        opacity: (1 - root.level) * 0.4

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.wellSettleDuration } }
    }

    // Moving the Well: press the space around the spheres, then drag past
    // the usual threshold, so clicks never nudge it. Only while it's awake.
    MouseArea {
        id: backgroundArea

        objectName: "dock.well.background"

        property real pressX: 0
        property real pressY: 0
        property real startX: 0
        property real startY: 0

        anchors.fill: parent
        preventStealing: true
        cursorShape: root.moving ? Qt.ClosedHandCursor : root.engaged ? Qt.OpenHandCursor : Qt.ArrowCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                wellMenu.popup(backgroundArea, mouse.x, mouse.y)
                return
            }
            var point = mapToItem(root.parent, mouse.x, mouse.y)
            pressX = point.x
            pressY = point.y
            startX = root.x
            startY = root.y
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !(pressedButtons & Qt.LeftButton)) {
                return
            }
            var point = mapToItem(root.parent, mouse.x, mouse.y)
            var dx = point.x - pressX
            var dy = point.y - pressY
            if (!root.moving) {
                if (Math.abs(dx) < Qt.styleHints.startDragDistance && Math.abs(dy) < Qt.styleHints.startDragDistance) {
                    return
                }
                root.moveX = startX
                root.moveY = startY
                root.moving = true
            }
            var box = root.frame
            root.moveX = root.rubber(startX + dx, box.x + root.margin, box.x + box.width - root.width - root.margin)
            root.moveY = root.rubber(startY + dy, box.y + root.margin, box.y + box.height - root.height - root.margin)
        }
        onReleased: {
            if (!root.moving) {
                return
            }
            var position = root.shareIn(root.frame, root.moveX, root.moveY, root.theme.px(18))
            root.settling = true
            settleTimer.restart()
            Docking.setWellPosition(root.wellId, position.x, position.y)
            root.moving = false
        }
        // Taken away mid-move (a window change, a grab elsewhere): it goes
        // back to where it was saved.
        onCanceled: root.moving = false
    }

    // Empty: a hollow sphere where things will land, and a word about it
    // once the pointer or a drag comes close.
    Item {
        visible: root.empty
        anchors.fill: parent
        opacity: root.shown

        Rectangle {
            id: ghost

            anchors.centerIn: parent
            width: Math.round(root.theme.px(40) * 0.84)
            height: width
            radius: width / 2
            scale: root.revealScale
            color: root.dropHere ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.18) : "transparent"
            border.width: 1
            border.color: root.dropHere || root.dragging
                ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, root.dropHere ? 0.8 : 0.45)
                : Qt.rgba(1, 1, 1, 0.14)

            Behavior on scale {
                enabled: !root.theme.reducedMotion
                SpringAnimation { spring: 7; damping: 0.42; epsilon: 0.002 }
            }

            AppIcon {
                anchors.centerIn: parent
                name: "archive"
                tone: root.dropHere ? "accent" : "muted"
                iconSize: 12
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.growDown ? parent.height + root.theme.px(4) : -height - root.theme.px(4)
            visible: root.engaged || root.dragging
            text: root.dropHere ? "Let go to put it away" : root.dragging ? "Drop here to minimize" : "Minimized tabs land here"
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }
    }

    // Between pinned and minimized: a faint seam that rides the wave.
    Rectangle {
        visible: root.geometry.divider !== null && !root.empty
        x: root.geometry.divider ? root.geometry.divider.x - width / 2 + root.wave.dividerShift : 0
        y: root.geometry.divider ? root.geometry.divider.y - height / 2 : 0
        width: 1
        height: root.theme.px(20)
        color: root.theme.panelBorder
        opacity: root.shown * 0.8
    }

    // An entry's sphere, as drawn now, in `item`'s coordinates (empty when
    // it isn't here). The Genie flies to and from it.
    function sphereRect(entryId, item) {
        for (var index = 0; index < icons.count; index += 1) {
            var icon = icons.itemAt(index)
            if (icon && icon.entryId === entryId) {
                var size = icon.orb * Math.max(0.5, icon.tileScale)
                var centre = icon.mapToItem(item, icon.width / 2 + icon.tileShift, icon.height / 2)
                return Qt.rect(centre.x - size / 2, centre.y - size / 2, size, size)
            }
        }
        return Qt.rect(0, 0, 0, 0)
    }

    function react(entryId, kind, duration) {
        for (var index = 0; index < icons.count; index += 1) {
            var icon = icons.itemAt(index)
            if (icon && icon.entryId === entryId) {
                icon.react(kind, duration)
            }
        }
    }

    Repeater {
        id: icons

        model: root.empty ? 0 : root.entries.length

        delegate: DockWellIcon {
            required property int index

            theme: root.theme
            workspace: root.workspace
            entry: root.entries[index] || ({})
            slot: root.geometry.slots[index] || ({ x: 0, y: 0, size: 0 })
            tileScale: root.revealScale * (root.wave.scales[index] || 1)
            tileShift: root.wave.shifts[index] || 0
            shown: root.shown
            lift: root.lift
            growDown: root.growDown
            vertical: root.vertical
            engaged: root.engaged || root.held
            onMenuRequested: function(entry, source, x, y) {
                entryMenu.entry = entry
                entryMenu.popup(source, x, y)
            }
        }
    }

    DockMenu {
        id: entryMenu

        property var entry: ({})
        readonly property string entryState: String(entry.state || "")
        readonly property var minimizedIds: entry.minimizedViewIds || []

        theme: root.theme

        DockMenuItem {
            theme: root.theme
            text: entryMenu.entryState === "minimized" ? "Restore" : entryMenu.entryState === "pinnedOpen" ? "Show" : "Open"
            onTriggered: root.workspace.restoreFromWell(String(entryMenu.entry.entryId || ""))
        }
        DockMenuSeparator { theme: root.theme; visible: !entryMenu.entry.panel }
        DockMenuItem {
            theme: root.theme
            text: entryMenu.entry.pinned ? "Unpin from Well" : "Keep in Well"
            visible: !entryMenu.entry.panel
            onTriggered: Docking.setWellEntryPinned(String(entryMenu.entry.entryId || ""), !entryMenu.entry.pinned)
        }
        DockMenuItem {
            theme: root.theme
            text: "Close"
            // One at a time, so each can ask about unsaved changes.
            visible: entryMenu.minimizedIds.length === 1 && !entryMenu.entry.panel
            onTriggered: root.workspace.closeView(String(entryMenu.minimizedIds[0] || ""))
        }
    }

    // While it's moved: a faint line on the centre of its frame when it's
    // close enough to land on it (it snaps there when let go).
    Rectangle {
        readonly property real centre: root.frame.x + root.frame.width / 2
        readonly property bool near: root.moving && Math.abs(root.x + root.width / 2 - centre) <= root.theme.px(18)

        x: centre - root.x - width / 2
        y: root.frame.y - root.y
        width: 1
        height: root.frame.height
        color: root.theme.accentBright
        opacity: near ? 0.35 : 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }
    }

    // The Well's own menu (right-click the space around its spheres).
    DockMenu {
        id: wellMenu

        theme: root.theme

        DockMenuItem {
            objectName: "well.menu.reactive"
            theme: root.theme
            text: "Follow the Layout"
            checkable: true
            checked: !root.fixed
            onTriggered: root.setFixed(false)
        }
        DockMenuItem {
            objectName: "well.menu.fixed"
            theme: root.theme
            text: "Stay Where Placed"
            checkable: true
            checked: root.fixed
            onTriggered: root.setFixed(true)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: "Move to Bottom Centre"
            onTriggered: {
                root.settling = true
                settleTimer.restart()
                Docking.setWellPosition(root.wellId, 0.5, 1)
            }
        }
    }
}
