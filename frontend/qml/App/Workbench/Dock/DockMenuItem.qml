import QtQuick
import QtQuick.Controls
import "../../Icons"

MenuItem {
    id: root

    required property var theme
    property string shortcutText: ""

    implicitHeight: visible ? 30 : 0
    leftPadding: 10
    rightPadding: 10

    // Checkable items get a check column; submenus get an arrow.
    indicator: Item {}
    arrow: Item {}

    contentItem: Item {
        implicitHeight: 30

        AppIcon {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: root.checkable && root.checked
            name: "check"
            tone: "accent"
            iconSize: 12
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: root.checkable ? 20 : 0
            anchors.right: trailing.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            elide: Text.ElideRight
            color: root.enabled ? root.theme.appText : root.theme.mutedText
            opacity: root.enabled ? 1 : 0.55
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Item {
            id: trailing

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: root.subMenu ? 12 : shortcut.implicitWidth
            height: parent.height

            Text {
                id: shortcut

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.subMenu
                text: root.shortcutText
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
            }

            AppIcon {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !!root.subMenu
                name: "chevron-right"
                tone: root.highlighted ? "normal" : "muted"
                iconSize: 10
            }
        }
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.highlighted && root.enabled ? root.theme.activeBg : "transparent"
    }
}
