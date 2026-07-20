import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    required property string title
    required property string glyph
    required property int depth
    required property bool selected
    required property bool muted
    required property bool folder
    required property bool expanded
    required property bool warning

    signal activated()

    width: parent ? parent.width : 220
    height: 23
    hoverEnabled: true
    padding: 0
    onClicked: activated()

    contentItem: Item {
        Text {
            x: 7 + root.depth * 13
            width: 10
            height: parent.height
            visible: root.folder
            text: root.expanded ? "⌄" : "›"
            color: root.hovered ? root.theme.appText : root.theme.mutedText
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 19 + root.depth * 13
            width: 15
            height: parent.height
            text: root.glyph
            color: root.muted ? "#756e63" : root.theme.mutedText
            font.pixelSize: root.folder ? 12 : 11
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 38 + root.depth * 13
            width: Math.max(0, parent.width - x - (root.warning ? 22 : 5))
            height: parent.height
            text: root.title
            color: root.muted ? "#756e63" : root.theme.appText
            font.pixelSize: 10
            font.strikeout: root.muted
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            visible: root.warning
            text: "△"
            color: root.theme.mutedText
            font.pixelSize: 10
        }
    }

    background: Rectangle {
        radius: 0
        color: root.selected
            ? root.theme.activeBg
            : root.hovered
                ? root.theme.hoverBg
                : "transparent"
        border.width: 0

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            visible: root.selected
            color: root.theme.accent
            opacity: 0.72
        }
    }

    scale: root.down ? 0.995 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }
}
