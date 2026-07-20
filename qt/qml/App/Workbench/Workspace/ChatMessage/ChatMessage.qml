import QtQuick

Item {
    id: root

    required property var theme
    required property string role
    required property string content
    required property string timestamp
    required property real leftObstruction

    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"
    readonly property real idealContentZoneWidth: Math.min(
        Math.max(0, width - theme.messageHorizontalInset * 2),
        theme.transcriptContentWidth
    )
    readonly property real centeredContentZoneX: Math.max(
        theme.messageHorizontalInset,
        (width - idealContentZoneWidth) / 2
    )
    readonly property real contentZoneX: Math.max(
        centeredContentZoneX,
        leftObstruction + theme.messageHorizontalInset
    )
    readonly property real contentZoneWidth: Math.max(
        0,
        Math.min(
            idealContentZoneWidth,
            width - theme.messageHorizontalInset - contentZoneX
        )
    )
    readonly property real desiredFrameWidth: userMessage
        ? theme.userMessageWidth
        : theme.assistantMessageWidth

    width: ListView.view ? ListView.view.width : 900
    height: frame.height + 18

    Item {
        id: frame

        x: root.userMessage
            ? root.contentZoneX + root.contentZoneWidth - width
            : root.contentZoneX
        width: Math.min(root.contentZoneWidth, root.desiredFrameWidth)
        height: messageColumn.implicitHeight

        Column {
            id: messageColumn

            width: parent.width
            spacing: 5

            Item {
                width: parent.width
                height: 24

                Row {
                    anchors.left: root.userMessage ? undefined : parent.left
                    anchors.right: root.userMessage ? parent.right : undefined
                    anchors.verticalCenter: parent.verticalCenter
                    layoutDirection: root.userMessage ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 7

                    Rectangle {
                        width: 20
                        height: 20
                        radius: 4
                        color: root.userMessage
                            ? "#1b1a17"
                            : root.systemMessage
                                ? "#1d1c19"
                                : "#22201c"

                        Text {
                            anchors.centerIn: parent
                            text: root.userMessage ? "Y" : root.systemMessage ? "!" : "✣"
                            color: root.systemMessage
                                ? root.theme.mutedText
                                : root.theme.appText
                            font.pixelSize: root.userMessage ? 8 : 10
                            font.weight: Font.Bold
                        }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        layoutDirection: root.userMessage ? Qt.RightToLeft : Qt.LeftToRight
                        spacing: 7

                        Text {
                            text: root.userMessage
                                ? "YOU"
                                : root.systemMessage
                                    ? "SYSTEM"
                                    : "ARCHIVIST"
                            color: root.theme.appText
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Text {
                            text: root.timestamp
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            opacity: 0.52
                        }
                    }
                }
            }

            Rectangle {
                id: surface

                width: parent.width
                height: messageText.implicitHeight + 24
                radius: 0
                color: root.userMessage
                    ? root.theme.userBg
                    : root.systemMessage
                        ? root.theme.systemBg
                        : root.theme.assistantBg
                border.width: 0
                antialiasing: false
                clip: true

                Text {
                    id: messageText

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 11
                    text: root.content
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: 13
                    lineHeight: 1.5
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    renderType: Text.NativeRendering
                }
            }
        }
    }
}
