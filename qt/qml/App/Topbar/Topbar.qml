import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme

    height: theme.topbarHeight
    color: theme.topbarBg
    border.width: 0
    z: 100

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.topbarBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 40
        anchors.rightMargin: 39
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "Archivist"
            color: root.theme.appText
            font.family: root.theme.titleFontFamily
            font.pixelSize: root.theme.typeSize(27)
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            Layout.preferredWidth: 35
            Layout.preferredHeight: 35
            radius: width / 2
            border.width: 1
            border.color: "#3c3656"
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#9d8ae8" }
                GradientStop { position: 0.45; color: "#6f70ca" }
                GradientStop { position: 1.0; color: "#2b6f69" }
            }

            Rectangle {
                x: 8
                y: 5
                width: 9
                height: 6
                radius: 4
                color: "#70ffffff"
                rotation: -18
            }

            HoverHandler {
                id: avatarHover
            }

            scale: avatarHover.hovered ? 1.08 : 1.0

            Behavior on scale {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }
    }
}
