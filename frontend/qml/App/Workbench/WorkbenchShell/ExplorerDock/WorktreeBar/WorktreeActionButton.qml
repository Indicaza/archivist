import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    property bool primary: false
    property bool danger: false

    implicitWidth: Math.max(72, label.implicitWidth + 20)
    implicitHeight: 26
    hoverEnabled: true
    padding: 0

    contentItem: Text {
        id: label

        text: root.text
        color: !root.enabled
            ? root.theme.mutedText
            : root.danger
                ? root.theme.danger
                : root.primary
                    ? root.theme.accentBright
                    : root.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
        opacity: root.enabled ? 1 : 0.45
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textMetadataSize
        font.weight: root.theme.textWeightStrong
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 3
        color: root.hovered && root.enabled
            ? (root.primary ? root.theme.activeBg : root.theme.hoverBg)
            : (root.primary ? root.theme.controlSurfaceBg : "transparent")
        border.width: root.primary ? 1 : 0
        border.color: !root.enabled
            ? root.theme.quietBorder
            : root.danger
                ? root.theme.danger
                : root.theme.accent
    }
}
