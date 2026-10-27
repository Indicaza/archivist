import QtQuick
import QtQuick.Controls

// "agent" with a breathing dot: an AI run is working in this worktree.
Row {
    id: root

    required property var theme
    property bool compact: false

    spacing: 4

    Accessible.role: Accessible.StaticText
    Accessible.name: "Agent running"

    ToolTip.visible: hover.hovered
    ToolTip.text: "An agent is running in this worktree"
    ToolTip.delay: 300

    HoverHandler {
        id: hover
    }

    Rectangle {
        id: dot

        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: root.theme.accent

        SequentialAnimation on opacity {
            running: root.visible
            loops: Animation.Infinite
            NumberAnimation { from: 1; to: 0.35; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.35; to: 1; duration: 700; easing.type: Easing.InOutSine }
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.compact
        text: "agent"
        color: root.theme.accentBright
        font.pixelSize: root.theme.typeSize(9)
    }
}
