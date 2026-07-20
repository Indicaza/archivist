import QtQuick

Item {
    id: root

    required property var theme
    required property string role
    required property string content
    required property string timestamp

    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"
    readonly property real contentZoneWidth: Math.min(
        Math.max(0, width - theme.messageHorizontalInset * 2),
        theme.transcriptContentWidth
    )
    readonly property real contentZoneX: theme.messageHorizontalInset
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
        scale: messageHover.hovered ? 1.003 : 1.0

        Column {
            id: messageColumn

            width: parent.width
            spacing: 6

            Item {
                width: parent.width
                height: 27

                Row {
                    id: messageHeader

                    anchors.left: root.userMessage ? undefined : parent.left
                    anchors.right: root.userMessage ? parent.right : undefined
                    anchors.verticalCenter: parent.verticalCenter
                    layoutDirection: root.userMessage ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 8

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 8
                        color: root.userMessage
                            ? "#241f18"
                            : root.systemMessage
                                ? "#24201a"
                                : root.theme.accentSoft
                        border.width: 1
                        border.color: root.userMessage
                            ? "#53432c"
                            : root.systemMessage
                                ? "#50442f"
                                : "#554a7b"

                        Text {
                            anchors.centerIn: parent
                            text: root.userMessage ? "Y" : root.systemMessage ? "!" : "✣"
                            color: root.userMessage
                                ? "#d1b17d"
                                : root.systemMessage
                                    ? root.theme.warning
                                    : root.theme.accentBright
                            font.pixelSize: root.userMessage ? 9 : 11
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
                            opacity: 0.58
                        }
                    }
                }
            }

            Rectangle {
                id: surface

                width: parent.width
                height: messageText.implicitHeight + 30
                radius: root.theme.radiusLarge
                color: root.userMessage
                    ? root.theme.userBg
                    : root.systemMessage
                        ? root.theme.systemBg
                        : root.theme.assistantBg
                border.width: 1
                border.color: root.userMessage
                    ? root.theme.userBorder
                    : root.systemMessage
                        ? "#4b4030"
                        : root.theme.assistantBorder

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: root.userMessage ? undefined : parent.left
                    anchors.right: root.userMessage ? parent.right : undefined
                    width: 3
                    radius: 2
                    gradient: Gradient {
                        GradientStop {
                            position: 0.0
                            color: root.userMessage
                                ? "#a6c49a5a"
                                : root.systemMessage
                                    ? "#94c49a5a"
                                    : "#b88f7adf"
                        }
                        GradientStop {
                            position: 1.0
                            color: root.userMessage
                                ? "#18c49a5a"
                                : root.systemMessage
                                    ? "#20c49a5a"
                                    : "#288f7adf"
                        }
                    }
                }

                Text {
                    id: messageText

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    anchors.topMargin: 14
                    text: root.content
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: 13
                    lineHeight: 1.55
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
