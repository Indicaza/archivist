import QtQuick

// "Today · 10:31 AM" between stretches of a conversation. Also used as the
// pinned label at the top of the transcript (without the hairlines), so it
// carries its own backdrop.
Item {
    id: root

    required property var theme
    required property string section
    property bool lines: true

    height: section.length > 0 ? 44 : 0
    visible: section.length > 0

    Rectangle {
        visible: root.lines
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: label.left
        anchors.leftMargin: Math.max(32, (parent.width - root.theme.transcriptContentWidth) / 2)
        anchors.rightMargin: 12
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: root.theme.quietBorder }
        }
    }

    Rectangle {
        id: label

        anchors.centerIn: parent
        width: labelText.implicitWidth + 22
        height: 22
        radius: 11
        color: root.theme.workspaceBg
        border.width: 1
        border.color: root.theme.quietBorder

        Text {
            id: labelText

            anchors.centerIn: parent
            text: root.section
            color: root.theme.mutedText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.DemiBold
            font.letterSpacing: 0.3
        }
    }

    Rectangle {
        visible: root.lines
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: label.right
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: Math.max(32, (parent.width - root.theme.transcriptContentWidth) / 2)
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: root.theme.quietBorder }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
