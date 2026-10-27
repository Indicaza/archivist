import QtQuick
import QtQuick.Controls

// A dialog action. The primary one is filled with the accent colour.
Button {
    id: root

    required property var theme
    property bool primary: false
    property bool destructive: false

    implicitHeight: 30
    implicitWidth: Math.max(78, label.implicitWidth + 28)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    contentItem: Text {
        id: label

        text: root.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.primary
            ? "#17151f"
            : root.destructive && root.hovered ? "#e08c84" : root.theme.appText
        opacity: root.enabled ? 1 : 0.5
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textCaptionSize
        font.weight: root.theme.textWeightEmphasis
    }

    background: Rectangle {
        radius: root.theme.radiusSmall + 1
        color: root.primary
            ? (root.down ? Qt.darker(root.theme.accentBright, 1.12) : root.hovered ? Qt.lighter(root.theme.accentBright, 1.06) : root.theme.accentBright)
            : (root.down ? root.theme.activeBg : root.hovered ? root.theme.hoverBg : "transparent")
        border.width: root.primary ? 0 : 1
        border.color: root.visualFocus ? root.theme.accent : root.theme.panelBorder

        Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
    }
}
