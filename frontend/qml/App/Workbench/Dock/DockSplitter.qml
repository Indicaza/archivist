import QtQuick
import Archivist.Services 1.0

// A divider between two neighbours of a split. Drag to resize (both sides
// keep their minimum size), double-click to share the space evenly.
Item {
    id: root

    required property var theme
    required property var workspace
    required property string nodeId
    required property int splitIndex
    required property string orientation
    required property real rectX
    required property real rectY
    required property real rectWidth
    required property real rectHeight

    readonly property bool horizontal: orientation === "horizontal"
    readonly property real grab: 8

    x: horizontal ? rectX - (grab - rectWidth) / 2 : rectX
    y: horizontal ? rectY : rectY - (grab - rectHeight) / 2
    width: horizontal ? Math.max(grab, rectWidth) : rectWidth
    height: horizontal ? rectHeight : Math.max(grab, rectHeight)
    z: 20
    visible: !Docking.dragActive

    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? (area.containsMouse || area.pressed ? 2 : 0) : parent.width
        height: root.horizontal ? parent.height : (area.containsMouse || area.pressed ? 2 : 0)
        radius: 1
        color: root.theme.accent
        opacity: area.pressed ? 0.95 : 0.7

        Behavior on width { NumberAnimation { duration: root.theme.motionFast } }
        Behavior on height { NumberAnimation { duration: root.theme.motionFast } }
    }

    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
        preventStealing: true

        onPressed: {
            Docking.setResizing(true)
            root.workspace.resizingSplit = true
        }
        onReleased: {
            Docking.setResizing(false)
            root.workspace.resizingSplit = false
        }
        onCanceled: {
            Docking.setResizing(false)
            root.workspace.resizingSplit = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }
            var point = mapToItem(root.workspace.area, mouse.x, mouse.y)
            // The model takes the leading edge of the divider.
            var thickness = Docking.splitterThickness
            Docking.resizeSplit(
                Docking.primarySurfaceId,
                root.nodeId,
                root.splitIndex,
                (root.horizontal ? point.x : point.y) - thickness / 2
            )
        }
        onDoubleClicked: Docking.equalizeSplit(root.nodeId)
    }
}
