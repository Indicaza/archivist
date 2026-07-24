import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    required property string glyph
    required property string label
    required property bool active
    required property bool neighborHovered

    width: 34
    height: 36
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    x: hovered ? 2 : neighborHovered ? 1 : 0
    scale: down
        ? theme.pressedScale
        : hovered
            ? theme.hoverScale
            : neighborHovered
                ? theme.hoverNeighborScale
                : 1.0
    z: hovered ? 3 : neighborHovered ? 2 : 1

    ToolTip.visible: hovered
    ToolTip.text: label
    ToolTip.delay: 350

    contentItem: Text {
        text: root.glyph
        color: root.active || root.hovered ? root.theme.appText : root.theme.mutedText
        font.pixelSize: root.theme.typeSize(17)
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.active ? root.theme.activeBg : root.hovered ? root.theme.hoverBg : "transparent"
        border.width: root.active || root.hovered ? 1 : 0
        border.color: root.active ? "#5a4d8c" : root.theme.panelBorder

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: -4
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 19
            radius: 1
            visible: root.active
            color: root.theme.accent
        }
    }

    Behavior on scale {
        enabled: !root.down

        NumberAnimation {
            duration: root.hovered || root.neighborHovered
                ? root.theme.motionHover
                : root.theme.motionHoverExit
            easing.type: Easing.OutCubic
        }
    }

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
}
