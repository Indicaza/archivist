import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property bool explorerOpen
    required property bool dockAttached

    height: theme.statusBarHeight
    color: theme.workspaceBgDeep

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.quietBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 9

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.theme.success
        }

        Text {
            text: "Ready"
            color: root.theme.mutedText
            font.pixelSize: 8
        }

        Text {
            text: "Local"
            color: root.theme.mutedText
            font.pixelSize: 8
            opacity: 0.72
        }

        Item {
            Layout.fillWidth: true
        }

        Text {
            text: root.explorerOpen ? "Explorer open" : "Explorer hidden"
            color: root.theme.mutedText
            font.pixelSize: 8
        }

        Text {
            text: root.dockAttached ? "Dock attached" : "Dock centered"
            color: root.theme.mutedText
            font.pixelSize: 8
        }

        Text {
            text: "Qt 6.11"
            color: root.theme.accentBright
            font.pixelSize: 8
        }
    }
}
