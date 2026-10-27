import QtQuick
import QtQuick.Controls

MenuItem {
    id: root

    required property var theme
    property string shortcutText: ""

    implicitHeight: visible ? 30 : 0
    leftPadding: 10
    rightPadding: 10

    contentItem: Item {
        implicitHeight: 30

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.enabled ? root.theme.appText : root.theme.mutedText
            opacity: root.enabled ? 1 : 0.55
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.shortcutText
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
        }
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.highlighted && root.enabled ? root.theme.activeBg : "transparent"
    }
}
