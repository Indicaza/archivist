import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One entry in a Gravity Well. The item is the entry's logical slot (its
// hit box, placed by WellGeometry); `tile` is what's drawn and the only
// part that may scale or move, so the slot under the pointer never shifts.
Item {
    id: root

    required property var theme
    required property var workspace
    required property var entry
    required property var slot

    // The drawn tile's size relative to the slot (magnification sets this).
    property real tileScale: 1
    // Drawn offset along the Well (neighbours stepping aside).
    property real tileShift: 0

    signal menuRequested(var entry, Item source, real x, real y)

    x: slot.x
    y: slot.y
    width: slot.size
    height: slot.size

    readonly property string entryId: String(entry.entryId || "")
    readonly property string entryState: String(entry.state || "minimized")
    readonly property var minimizedIds: entry.minimizedViewIds || []
    readonly property var openIds: entry.openViewIds || []
    readonly property var resource: entry.resource || ({})
    readonly property var info: ({
        viewId: String(minimizedIds[minimizedIds.length - 1] || openIds[0] || entryId),
        typeId: String(entry.typeId || ""),
        title: String(entry.title || ""),
        resource: resource
    })
    readonly property string title: {
        ChatStore.chats
        var name = root.workspace.viewTitle(info)
        return name.length > 0 ? name : "Untitled"
    }
    readonly property bool minimized: minimizedIds.length > 0
    readonly property bool open: openIds.length > 0
    readonly property bool focusedHere: {
        Docking.revision
        return openIds.indexOf(Docking.focusedViewId) >= 0
    }
    readonly property bool dirty: {
        Documents.revision
        return resource.kind === "document" && Documents.isDirty(String(resource.id || ""))
    }
    readonly property string stateText: entryState === "minimized"
        ? (minimizedIds.length > 1 ? minimizedIds.length + " minimized · click to restore" : "Minimized · click to restore")
        : entryState === "pinnedOpen"
            ? "Pinned · open"
            : "Pinned · click to open"
    readonly property bool hovered: hit.containsMouse

    Accessible.role: Accessible.Button
    Accessible.name: title + ", " + stateText + (dirty ? ", unsaved changes" : "")

    Item {
        id: tile

        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        scale: root.tileScale
        transform: Translate { x: root.tileShift }
        // Grows from its base, like a Dock icon.
        transformOrigin: Item.Bottom

        Rectangle {
            id: plate

            anchors.fill: parent
            anchors.margins: root.theme.px(2)
            radius: root.theme.radiusMedium
            color: hit.pressed ? root.theme.activeBg
                : root.hovered ? root.theme.hoverBg
                : root.focusedHere ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.14)
                : Qt.rgba(1, 1, 1, 0.025)
            border.width: 1
            border.color: root.hovered || root.focusedHere
                ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, root.hovered ? 0.42 : 0.28)
                : Qt.rgba(1, 1, 1, 0.05)

            Behavior on color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionFast } }
            Behavior on border.color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionFast } }
        }

        AppIcon {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -root.theme.px(1)
            name: root.workspace.viewIconName(root.info)
            tone: root.hovered || root.focusedHere ? "accent" : root.entryState === "pinnedClosed" ? "muted" : "normal"
            iconSize: 17
            opacity: root.entryState === "pinnedClosed" && !root.hovered ? 0.75 : 1
            accessibleLabel: root.title
        }

        // Unsaved changes: a small mark in the corner, as on its tab.
        Rectangle {
            visible: root.dirty
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: root.theme.px(5)
            width: root.theme.px(6)
            height: width
            radius: width / 2
            color: root.theme.warning
        }
    }

    // Under the icon: a filled dot when it's open somewhere, a ring when
    // it's minimized (put away, still running), nothing when it's only pinned.
    Rectangle {
        visible: root.open || root.minimized
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -root.theme.px(3)
        width: root.theme.px(4)
        height: width
        radius: width / 2
        color: root.open ? (root.focusedHere ? root.theme.accentBright : root.theme.mutedText) : "transparent"
        border.width: root.open ? 0 : 1
        border.color: root.theme.mutedText
    }

    MouseArea {
        id: hit

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.menuRequested(root.entry, root, mouse.x, mouse.y)
                return
            }
            Docking.activateWellEntry(root.entryId)
        }
    }

    ToolTip.visible: hit.containsMouse && !hit.pressed
    ToolTip.delay: 350
    ToolTip.text: root.title + "\n" + root.stateText
}
