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
    required property bool neighborHovered

    signal activated()

    width: parent ? parent.width : 220
    height: 27
    hoverEnabled: true
    padding: 0
    onClicked: activated()

    contentItem: Item {
        x: root.hovered ? 3 : root.neighborHovered ? 1.5 : 0

        Behavior on x {
            NumberAnimation {
                duration: root.hovered || root.neighborHovered
                    ? root.theme.motionHover
                    : root.theme.motionHoverExit
                easing.type: root.hovered || root.neighborHovered
                    ? Easing.OutBack
                    : Easing.OutCubic
            }
        }

        Text {
            x: 7 + root.depth * 13
            width: 10
            height: parent.height
            visible: root.folder
            text: root.expanded ? "⌄" : "›"
            color: root.hovered ? root.theme.appText : root.theme.mutedText
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 19 + root.depth * 13
            width: 15
            height: parent.height
            text: root.glyph
            color: root.muted ? "#756e63" : root.theme.mutedText
            font.pixelSize: root.folder ? 14 : 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 38 + root.depth * 13
            width: Math.max(0, parent.width - x - (root.warning ? 22 : 5))
            height: parent.height
            text: root.title
            color: root.muted ? "#756e63" : root.theme.appText
            font.pixelSize: 11
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
            font.pixelSize: 11
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

        Behavior on color {
            ColorAnimation { duration: root.theme.motionFast }
        }
    }

    transformOrigin: Item.Left
    scale: root.down
        ? root.theme.pressedScale
        : root.hovered
            ? root.theme.hoverScale
            : root.neighborHovered
                ? root.theme.hoverNeighborScale
                : 1.0
    z: root.hovered ? 3 : root.neighborHovered ? 2 : 1

    Behavior on scale {
        NumberAnimation {
            duration: root.hovered || root.neighborHovered
                ? root.theme.motionHover
                : root.theme.motionHoverExit
            easing.type: root.hovered || root.neighborHovered
                ? Easing.OutBack
                : Easing.OutCubic
        }
    }
}
