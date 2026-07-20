import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme

    width: 34
    height: 34
    hoverEnabled: true
    padding: 0

    ToolTip.visible: hovered
    ToolTip.text: "Jump to latest"
    ToolTip.delay: 350

    contentItem: Text {
        text: "↓"
        color: parent.hovered ? root.theme.appText : root.theme.mutedText
        font.pixelSize: 16
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 8
        color: parent.hovered ? "#29261f" : "#201e1a"
        border.width: 1
        border.color: parent.hovered ? "#5b4d8c" : root.theme.panelBorder
    }

    scale: down ? 0.96 : hovered ? 1.05 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }
}
