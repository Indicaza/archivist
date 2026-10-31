import QtQuick
import Archivist.Services 1.0

// The draggable right edge of a side panel (the Explorer, Home's sidebar).
// Drag to resize; double-click for the default width. The shell owns the
// width: this only reports where the pointer is, in `coordinateItem`.
Item {
    id: root

    required property var theme
    required property Item coordinateItem

    readonly property bool resizing: resizeArea.pressed

    // The Observatory live recorder's span for the current drag (0 when not recording).
    property real liveResize: 0

    signal resizeRequested(real pointerX)
    signal resetRequested()

    width: theme.resizeHandleThickness

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: resizeArea.containsMouse || resizeArea.pressed ? 2 : 1
        height: parent.height
        color: resizeArea.containsMouse || resizeArea.pressed
            ? root.theme.accent
            : root.theme.panelBorder
        opacity: resizeArea.containsMouse || resizeArea.pressed
            ? 0.9
            : 0.52

        Behavior on color {
            ColorAnimation { duration: root.theme.motionFast }
        }
    }

    MouseArea {
        id: resizeArea

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        onPressedChanged: {
            if (pressed) {
                root.liveResize = LiveRecorder.begin("sidebar", "resize")
            } else {
                LiveRecorder.end(root.liveResize, true)
                root.liveResize = 0
            }
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }

            const point = mapToItem(root.coordinateItem, mouse.x, mouse.y)
            root.resizeRequested(point.x)
        }
        onDoubleClicked: root.resetRequested()
    }
}
