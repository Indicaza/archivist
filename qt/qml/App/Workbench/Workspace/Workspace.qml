import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ChatMessage"

Rectangle {
    id: root

    required property var theme
    property real leftInset: 0

    color: theme.surfaceBg
    clip: true

    function appendUserMessage(message) {
        var trimmed = message.trim()

        if (trimmed.length === 0) {
            return
        }

        messages.append({
            roleValue: "user",
            contentValue: trimmed,
            timestampValue: "Now"
        })

        messages.append({
            roleValue: "assistant",
            contentValue: "The native Qt Workspace received that message. Backend wiring comes after this visual and structural slice is stable.",
            timestampValue: "Now"
        })

        Qt.callLater(function() {
            transcript.positionViewAtEnd()
        })
    }

    function seedMessages() {
        if (messages.count > 0) {
            return
        }

        messages.append({
            roleValue: "system",
            contentValue: "Qt Workbench prototype · local visual data only · Express and SQLite remain untouched.",
            timestampValue: "7:42 PM"
        })

        for (var index = 0; index < 42; index += 1) {
            var userTurn = index % 2 === 0
            var cycle = Math.floor(index / 2) + 1

            messages.append({
                roleValue: userTurn ? "user" : "assistant",
                contentValue: userTurn
                    ? "Synthetic native transcript turn " + cycle + ". This scroll exists to exercise variable-height delegates without creating a DOM node for the entire history."
                    : "Archivist response " + cycle + ". Qt Quick ListView creates and reuses delegates around the visible viewport, giving this migration a native virtualization foundation.",
                timestampValue: "Demo " + String(index + 1)
            })
        }

        Qt.callLater(function() {
            transcript.positionViewAtEnd()
        })
    }

    Component.onCompleted: seedMessages()

    Rectangle {
        id: workspaceHeader

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        color: theme.controlSurfaceBg

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14 + root.leftInset
            anchors.rightMargin: 14
            spacing: 8

            Text {
                text: "Archivist"
                color: root.theme.mutedText
                font.pixelSize: 9
            }

            Text {
                text: "/"
                color: root.theme.mutedText
                font.pixelSize: 9
                opacity: 0.55
            }

            Text {
                Layout.fillWidth: true
                text: "Qt Workbench Foundation"
                color: root.theme.appText
                font.pixelSize: 10
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.preferredWidth: 72
                Layout.preferredHeight: 22
                radius: 11
                color: root.theme.accentSoft
                border.width: 1
                border.color: "#4c426d"

                Text {
                    anchors.centerIn: parent
                    text: "NATIVE"
                    color: root.theme.accentBright
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }
            }
        }
    }

    ListView {
        id: transcript

        anchors.top: workspaceHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
        clip: true
        spacing: 10
        topMargin: 18
        bottomMargin: 22
        cacheBuffer: 1200
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        model: ListModel {
            id: messages
        }

        delegate: ChatMessage {
            required property string roleValue
            required property string contentValue
            required property string timestampValue

            width: transcript.width
            theme: root.theme
            role: roleValue
            content: contentValue
            timestamp: timestampValue
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}
