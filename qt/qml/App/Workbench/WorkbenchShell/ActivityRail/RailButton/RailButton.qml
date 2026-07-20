import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    required property string glyph
    required property string label
    required property bool active

    width: 30
    height: 31
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    scale: hovered ? 1.06 : 1.0

    ToolTip.visible: hovered
    ToolTip.text: label
    ToolTip.delay: 350

    contentItem: Text {
        text: root.glyph
        color: root.active || root.hovered ? root.theme.appText : root.theme.mutedText
        font.pixelSize: 15
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
        NumberAnimation { duration: 170; easing.type: Easing.OutBack }
    }
}
