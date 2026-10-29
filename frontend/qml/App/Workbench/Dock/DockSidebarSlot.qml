import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// A spot in the sidebar that holds tabs and panels: one of the rail's tabs,
// or the bottom of the sidebar. It holds one surface of the docking
// workspace, with its own tabs and splits. Empty, it is a drop zone: drop a
// tab or a panel on it and it docks here (no need to hold it there).
Item {
    id: root

    required property var theme
    // The DockWorkspace (null while the old workspace is in use).
    property var workspace: null
    // A rail tab's id, or "sidebar.bottom".
    property string slot: ""
    property string emptyTitle: "Nothing docked here yet"
    property string emptyHint: "Drag a tab or a panel here to dock it."
    // A slim strip (the bottom of the sidebar) instead of a whole panel.
    property bool compact: false

    readonly property string slotSurfaceId: {
        Docking.revision
        Docking.sidebarSurfaceIds
        return root.slot.length > 0 ? Docking.sidebarSurface(root.slot) : ""
    }
    readonly property bool empty: slotSurfaceId.length === 0
    // A drag is over this spot and a drop would dock here.
    readonly property bool aimed: Docking.dragActive && String(Docking.dragState.slot || "") === root.slot
    readonly property bool acceptsDrop: root.empty && root.visible && root.enabled && root.width > 0 && root.height > 0
        && root.workspace !== null

    function containsGlobal(point) {
        if (!root.visible || !root.Window.window) {
            return false
        }
        var local = root.mapFromGlobal(point.x, point.y)
        return local.x >= 0 && local.y >= 0 && local.x <= width && local.y <= height
    }

    property var registeredWith: null
    property string registeredSlot: ""

    function register() {
        if (root.registeredWith && root.registeredWith.unregisterSlotZone) {
            root.registeredWith.unregisterSlotZone(root.registeredSlot, root)
        }
        root.registeredWith = root.workspace && root.workspace.registerSlotZone ? root.workspace : null
        root.registeredSlot = root.slot
        if (root.registeredWith && root.slot.length > 0) {
            root.registeredWith.registerSlotZone(root.slot, root)
        }
    }

    onWorkspaceChanged: {
        register()
        syncShown()
    }
    onSlotChanged: register()
    Component.onDestruction: {
        if (root.registeredWith && root.registeredWith.unregisterSlotZone) {
            root.registeredWith.unregisterSlotZone(root.registeredSlot, root)
        }
    }

    objectName: "dock.sidebarSlot"

    // What is docked here. The area is made once per surface and never
    // rebuilt while it shows: a rebuild in the middle of a drag would drop
    // the pointer of whatever is being dragged out of it. A different
    // surface gets a fresh area.
    property string shownSurfaceId: ""
    function syncShown() {
        var wanted = root.workspace ? root.slotSurfaceId : ""
        if (wanted === root.shownSurfaceId) {
            return
        }
        // Empty first, so the old area goes before the new one comes.
        root.shownSurfaceId = ""
        root.shownSurfaceId = wanted
    }
    onSlotSurfaceIdChanged: syncShown()
    Component.onCompleted: {
        register()
        syncShown()
    }

    Repeater {
        model: root.shownSurfaceId.length > 0 ? 1 : 0

        delegate: DockSurfaceArea {
            x: 0
            y: 0
            width: root.width
            height: root.height
            theme: root.theme
            workspace: root.workspace
            // Set once, not bound: the area keeps its surface until it goes.
            Component.onCompleted: surfaceId = root.shownSurfaceId
        }
    }

    // Empty: where to drop.
    Rectangle {
        id: zone

        objectName: "dock.sidebarSlot.zone"
        anchors.fill: parent
        anchors.margins: root.compact ? 6 : 10
        visible: root.empty
        radius: root.theme.radiusPanel
        color: root.aimed
            ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.14)
            : Docking.dragActive ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.05)
            : "transparent"
        border.width: root.aimed ? 1.5 : 1
        border.color: root.aimed ? root.theme.accentBright
            : Docking.dragActive ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.45)
            : root.theme.quietBorder

        Behavior on color {
            ColorAnimation { duration: root.theme.reducedMotion ? 0 : 120 }
        }

        Column {
            anchors.centerIn: parent
            width: Math.max(120, parent.width - 24)
            spacing: root.compact ? 2 : 8
            visible: parent.height > 30

            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !root.compact
                name: "grid"
                tone: root.aimed ? "accent" : "muted"
                iconSize: 22
            }

            Text {
                width: parent.width
                visible: !root.compact
                text: root.aimed ? "Release to dock here" : root.emptyTitle
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeBody
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                visible: root.compact || !root.aimed
                text: root.compact
                    ? (root.aimed ? "Release to dock at the bottom" : "Drop here to dock at the bottom")
                    : root.emptyHint
                color: root.aimed ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
