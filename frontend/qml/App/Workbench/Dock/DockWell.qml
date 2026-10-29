import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"
import "WellGeometry.js" as WellGeometry

// A Gravity Well: a small, movable row of what's been minimized or pinned.
// It shows references the workspace holds (Docking.wells()); clicking an
// entry restores, brings forward or reopens it (WorkspaceStore). It is an
// accessory over the layout, not a panel, and never reflows the layout.
//
// Pinned entries come first, then a divider, then what's only minimized.
// Grab the grip or the Well's edge to move it; it's saved with the layout.
Item {
    id: root

    objectName: "dock.well"

    required property var theme
    required property var workspace
    // One row of Docking.wells().
    required property var well
    // Room kept free at the bottom (the compact chat composer floats there).
    property real bottomClearance: 0

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

    readonly property real margin: theme.px(12)
    readonly property real gripWidth: theme.px(14)
    readonly property var geometry: WellGeometry.layout(
        String(well.form || "linear"),
        String(well.orientation || "horizontal"),
        entries.length,
        {
            slot: theme.px(40),
            gap: theme.px(4),
            padding: theme.px(6),
            lead: gripWidth,
            dividerAt: pinnedCount > 0 && pinnedCount < entries.length ? pinnedCount : -1,
            dividerSize: theme.px(13)
        }
    )

    // While it's being moved: where the pointer has it. Otherwise: where it
    // was saved, worked out again for the current window size.
    property bool moving: false
    property real moveX: 0
    property real moveY: 0
    readonly property var rest: WellGeometry.place(
        { x: Number(well.x), y: Number(well.y) },
        width, height,
        parent ? parent.width : 0, parent ? parent.height : 0,
        margin, bottomClearance
    )

    x: moving ? moveX : rest.x
    y: moving ? moveY : rest.y
    width: empty ? emptyRow.width + gripWidth + theme.px(22) : geometry.width
    height: empty ? theme.px(34) : geometry.height
    z: moving ? 2 : 0

    // Settles onto the centre line or an edge after a drop.
    Behavior on x {
        enabled: !root.moving && !root.theme.reducedMotion
        SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 }
    }
    Behavior on y {
        enabled: !root.moving && !root.theme.reducedMotion
        SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 }
    }

    Accessible.role: Accessible.ToolBar
    Accessible.name: empty ? "Gravity Well, empty" : "Gravity Well, " + entries.length + (entries.length === 1 ? " item" : " items")

    // Shadow, lifted a little while it moves.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.theme.px(6)
        anchors.bottomMargin: -root.theme.px(8)
        anchors.leftMargin: root.theme.px(2)
        anchors.rightMargin: root.theme.px(2)
        radius: body.radius + 2
        color: "#000000"
        opacity: root.empty ? 0.12 : root.moving && !root.theme.reducedMotion ? 0.42 : 0.26
        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.magnetPickupDuration } }
    }

    Rectangle {
        id: body

        anchors.fill: parent
        radius: root.empty ? height / 2 : root.theme.radiusLarge
        color: Qt.rgba(root.theme.compactComposerBg.r, root.theme.compactComposerBg.g, root.theme.compactComposerBg.b, root.empty ? 0.72 : 0.9)
        border.width: 1
        border.color: root.moving || backgroundArea.containsMouse ? root.theme.compactComposerBorderHover : root.theme.compactComposerBorder
        opacity: root.empty && !backgroundArea.containsMouse && !root.moving ? 0.7 : 1

        Behavior on border.color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionFast } }
        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionHover } }

        // A faint top light, like the composer.
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - parent.radius * 2
            height: 1
            color: root.theme.compactComposerHighlight
        }
    }

    // Moving the Well: press anywhere that isn't an icon, then drag past
    // the usual threshold, so clicks never nudge it.
    MouseArea {
        id: backgroundArea

        objectName: "dock.well.background"

        property real pressX: 0
        property real pressY: 0
        property real startX: 0
        property real startY: 0

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.moving ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        onPressed: function(mouse) {
            var point = mapToItem(root.parent, mouse.x, mouse.y)
            pressX = point.x
            pressY = point.y
            startX = root.x
            startY = root.y
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
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
            var areaWidth = root.parent ? root.parent.width : 0
            var areaHeight = root.parent ? root.parent.height : 0
            root.moveX = Math.max(root.margin, Math.min(areaWidth - root.width - root.margin, startX + dx))
            root.moveY = Math.max(root.margin, Math.min(areaHeight - root.height - root.margin - root.bottomClearance, startY + dy))
        }
        onReleased: {
            if (!root.moving) {
                return
            }
            var position = WellGeometry.position(
                root.moveX, root.moveY, root.width, root.height,
                root.parent ? root.parent.width : 0, root.parent ? root.parent.height : 0,
                root.margin, root.bottomClearance, root.theme.px(18)
            )
            Docking.setWellPosition(root.wellId, position.x, position.y)
            root.moving = false
        }
        // Taken away mid-move (a window change, a grab elsewhere): it goes
        // back to where it was saved.
        onCanceled: root.moving = false
    }

    // The grip: where to grab it.
    Grid {
        anchors.left: parent.left
        anchors.leftMargin: root.theme.px(7)
        anchors.verticalCenter: parent.verticalCenter
        columns: 2
        rows: 3
        spacing: root.theme.px(2)
        opacity: root.moving ? 1 : backgroundArea.containsMouse ? 0.85 : 0.45

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionFast } }

        Repeater {
            model: 6

            Rectangle {
                width: root.theme.px(2.5)
                height: width
                radius: width / 2
                color: root.moving ? root.theme.accentBright : root.theme.mutedText
            }
        }
    }

    // Empty: say what it's for.
    Row {
        id: emptyRow

        visible: root.empty
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: root.gripWidth + root.theme.px(10)
        spacing: root.theme.px(6)

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "archive"
            tone: "muted"
            iconSize: 12
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Minimized tabs land here"
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }
    }

    // Between pinned and minimized.
    Rectangle {
        visible: root.geometry.divider !== null && !root.empty
        x: root.geometry.divider ? root.geometry.divider.x - width / 2 : 0
        y: root.geometry.divider ? root.geometry.divider.y - height / 2 : 0
        width: 1
        height: root.theme.px(24)
        color: root.theme.panelBorder
    }

    Repeater {
        model: root.empty ? 0 : root.entries.length

        delegate: DockWellIcon {
            required property int index

            theme: root.theme
            workspace: root.workspace
            entry: root.entries[index]
            slot: root.geometry.slots[index]
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
            onTriggered: Docking.activateWellEntry(String(entryMenu.entry.entryId || ""))
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: entryMenu.entry.pinned ? "Unpin from Well" : "Keep in Well"
            onTriggered: Docking.setWellEntryPinned(String(entryMenu.entry.entryId || ""), !entryMenu.entry.pinned)
        }
        DockMenuItem {
            theme: root.theme
            text: "Close"
            // One at a time, so each can ask about unsaved changes.
            visible: entryMenu.minimizedIds.length === 1
            onTriggered: root.workspace.closeView(String(entryMenu.minimizedIds[0] || ""))
        }
    }
}
