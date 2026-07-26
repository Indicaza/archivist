import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

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
        anchors.leftMargin: 32
        anchors.rightMargin: 31
        spacing: 8

        Text {
            Layout.preferredWidth: 146
            text: "Archivist"
            color: root.theme.appText
            font.family: root.theme.titleFontFamily
            font.pixelSize: root.theme.textTopbarBrandSize
            font.weight: root.theme.textWeightEmphasis
            verticalAlignment: Text.AlignVCenter
        }

        Item {
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.preferredWidth: root.theme.topbarAvatarSize
            Layout.preferredHeight: root.theme.topbarAvatarSize
            radius: width / 2
            border.width: 1
            border.color: "#3c3656"
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#9d8ae8" }
                GradientStop { position: 0.45; color: "#6f70ca" }
                GradientStop { position: 1.0; color: "#2b6f69" }
            }

            Rectangle {
                x: 6
                y: 4
                width: 7
                height: 5
                radius: 4
                color: "#70ffffff"
                rotation: -18
            }

            HoverHandler {
                id: avatarHover
            }

            scale: avatarHover.hovered ? 1.08 : 1.0

            Behavior on scale {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
