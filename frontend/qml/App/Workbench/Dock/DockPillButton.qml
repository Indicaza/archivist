import QtQuick
import QtQuick.Controls
import "../../Icons"

// A quiet rounded button for empty states and cards.
Button {
    id: root

    required property var theme
    property string iconName: ""

    implicitHeight: 28
    leftPadding: iconName.length > 0 ? 10 : 12
    rightPadding: 12
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    contentItem: Row {
        spacing: 6

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName.length > 0
            name: root.iconName
            tone: root.hovered ? "accent" : "muted"
            iconSize: 13
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.hovered ? root.theme.appText : root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            font.weight: root.theme.textWeightEmphasis
        }
    }

    background: Rectangle {
        radius: height / 2
        color: root.down ? root.theme.activeBg : root.hovered ? root.theme.hoverBg : "transparent"
        border.width: 1
        border.color: root.hovered ? root.theme.accent : root.theme.panelBorder

        Behavior on border.color { ColorAnimation { duration: root.theme.motionFast } }
        Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
    }
}
