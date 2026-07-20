import QtQuick

Item {
    id: root

    required property var theme
    required property string role
    required property string content
    required property string timestamp

    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"

    width: ListView.view ? ListView.view.width : 900
    height: frame.height + 14

    Item {
        id: frame

        x: root.userMessage ? root.width - width - 24 : 24
        width: Math.min(root.width - 48, root.userMessage ? 680 : 920)
        height: messageColumn.implicitHeight
        scale: messageHover.hovered ? 1.006 : 1.0

        Column {
            id: messageColumn

            width: parent.width
            spacing: 6

            Row {
                anchors.right: root.userMessage ? parent.right : undefined
                anchors.left: root.userMessage ? undefined : parent.left
                layoutDirection: root.userMessage ? Qt.RightToLeft : Qt.LeftToRight
                spacing: 8

                Rectangle {
                    width: 24
                    height: 24
                    radius: 8
                    color: root.userMessage ? "#241f18" : root.systemMessage ? "#24201a" : root.theme.accentSoft
                    border.width: 1
                    border.color: root.userMessage ? "#53432c" : root.systemMessage ? "#50442f" : "#554a7b"

                    Text {
                        anchors.centerIn: parent
                        text: root.userMessage ? "Y" : root.systemMessage ? "!" : "A"
                        color: root.userMessage ? "#d1b17d" : root.systemMessage ? root.theme.warning : root.theme.accentBright
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                }

                Column {
                    spacing: 1

                    Text {
                        text: root.userMessage ? "YOU" : root.systemMessage ? "SYSTEM" : "ARCHIVIST"
                        color: root.theme.appText
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Text {
                        text: root.timestamp
                        color: root.theme.mutedText
                        font.pixelSize: 8
                        opacity: 0.58
                    }
                }
            }

            Rectangle {
                id: surface

                width: parent.width
                height: messageText.implicitHeight + 30
                radius: root.theme.radiusLarge
                color: root.userMessage ? root.theme.userBg : root.systemMessage ? root.theme.systemBg : root.theme.assistantBg
                border.width: 1
                border.color: root.userMessage ? root.theme.userBorder : root.systemMessage ? "#4b4030" : root.theme.assistantBorder

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: root.userMessage ? undefined : parent.left
                    anchors.right: root.userMessage ? parent.right : undefined
                    width: 3
                    radius: 2
                    color: root.userMessage ? root.theme.userAccent : root.systemMessage ? root.theme.warning : root.theme.accent
                    opacity: 0.72
                }

                Text {
                    id: messageText

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 15
                    text: root.content
                    color: root.theme.appText
                    font.pixelSize: 13
                    lineHeight: 1.42
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
        }

        HoverHandler {
            id: messageHover
        }

        Behavior on scale {
            NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
        }
    }
}
