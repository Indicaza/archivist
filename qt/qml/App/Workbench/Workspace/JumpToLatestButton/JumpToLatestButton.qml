import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme

    width: 32
    height: 32
    hoverEnabled: true
    padding: 0

    ToolTip.visible: hovered
    ToolTip.text: "Jump to latest"
    ToolTip.delay: 350

    contentItem: Text {
        text: "↓"
        color: parent.hovered ? root.theme.appText : root.theme.mutedText
        font.pixelSize: 15
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 4
        color: parent.hovered ? "#292621" : "#201e1a"
        border.width: 0
    }

    scale: down ? 0.97 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }
}
