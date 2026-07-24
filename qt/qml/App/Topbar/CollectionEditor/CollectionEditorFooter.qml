import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property bool editing
    required property bool busy
    required property bool canSave
    required property bool confirmingArchive

    signal archiveRequested()
    signal cancelRequested()
    signal saveRequested()

    Layout.fillWidth: true
    Layout.preferredHeight: 60
    color: "#1a1916"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.quietBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 10

        Button {
            visible: root.editing
            Layout.preferredWidth: root.confirmingArchive ? 178 : 98
            Layout.preferredHeight: 34
            text: root.confirmingArchive
                ? "Archive this Collection?"
                : "Archive"
            enabled: !root.busy
            hoverEnabled: true
            onClicked: root.archiveRequested()

            background: Rectangle {
                color: parent.hovered ? "#342522" : "transparent"
                border.width: 1
                border.color: root.theme.danger
                radius: 5
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Button {
            Layout.preferredWidth: 86
            Layout.preferredHeight: 34
            text: "Cancel"
            enabled: !root.busy
            onClicked: root.cancelRequested()
        }

        Button {
            Layout.preferredWidth: 116
            Layout.preferredHeight: 34
            text: root.busy
                ? "Saving…"
                : root.editing
                    ? "Save"
                    : "Create"
            enabled: root.canSave
            onClicked: root.saveRequested()

            background: Rectangle {
                color: parent.enabled
                    ? parent.hovered
                        ? "#625783"
                        : "#51486e"
                    : root.theme.controlSurfaceBg
                border.width: 1
                border.color: parent.enabled
                    ? root.theme.accent
                    : root.theme.quietBorder
                radius: 5
            }
        }
    }
}
