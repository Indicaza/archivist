import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    required property string title
    required property string subtitle
    required property int depth
    required property bool selected

    width: parent ? parent.width : 220
    height: 54
    hoverEnabled: true
    leftPadding: 8 + depth * 12
    rightPadding: 8
    topPadding: 5
    bottomPadding: 5

    contentItem: Column {
        spacing: 3

        Text {
            width: parent.width
            text: root.title
            color: root.theme.appText
            font.pixelSize: 11
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: root.subtitle
            color: root.theme.mutedText
            font.pixelSize: 8
            opacity: 0.72
            elide: Text.ElideMiddle
        }
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.selected ? root.theme.activeBg : root.hovered ? root.theme.hoverBg : "transparent"
        border.width: root.selected || root.hovered ? 1 : 0
        border.color: root.selected ? "#53477e" : root.theme.quietBorder
    }
}
