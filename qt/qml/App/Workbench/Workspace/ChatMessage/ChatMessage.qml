import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var theme
    required property string messageId
    required property string role
    required property string content
    required property string timestamp
    required property string status
    required property real leftObstruction

    signal contextInspectionRequested(string messageId)

    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"
    readonly property bool streamingMessage: status === "streaming"
    readonly property bool failedMessage: status === "failed"
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
        ? Math.min(
            theme.userMessageWidth,
            content.indexOf("\n") >= 0 || content.length > 88
                ? theme.userMessageWidth
                : Math.max(240, content.length * 7.2 + 54)
        )
        : theme.assistantMessageWidth

    width: ListView.view ? ListView.view.width : 900
    height: frame.height + 28

    Item {
        id: frame

        x: root.userMessage
            ? root.contentZoneX + root.contentZoneWidth - width
            : root.contentZoneX
        width: Math.min(root.contentZoneWidth, root.desiredFrameWidth)
        height: messageColumn.implicitHeight

        Behavior on x {
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Column {
            id: messageColumn

            width: parent.width
            spacing: 10

            Item {
                width: parent.width
                height: 30

                Row {
                    anchors.left: root.userMessage ? undefined : parent.left
                    anchors.right: root.userMessage ? parent.right : undefined
                    anchors.verticalCenter: parent.verticalCenter
                    layoutDirection: root.userMessage ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 7

                    Rectangle {
                        width: 22
                        height: 22
                        radius: 5
                        color: root.userMessage
                            ? "#1b1a17"
                            : root.systemMessage
                                ? "#1d1c19"
                                : "#22201c"

                        Text {
                            anchors.centerIn: parent
                            text: root.userMessage ? "Y" : root.systemMessage ? "!" : "✣"
                            color: root.theme.appText
                            font.pixelSize: root.userMessage ? 9 : 11
                            font.weight: Font.Bold
                            opacity: root.streamingMessage ? 0.62 : 1
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
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Text {
                            text: root.failedMessage
                                ? "FAILED"
                                : root.streamingMessage
                                    ? "WORKING"
                                    : root.timestamp
                            color: root.failedMessage
                                ? root.theme.danger
                                : root.theme.mutedText
                            font.pixelSize: 9
                            opacity: root.failedMessage ? 0.9 : 0.52
                        }
                    }

                    Button {
                        width: 64
                        height: 22
                        visible: !root.userMessage
                            && !root.systemMessage
                            && !root.streamingMessage
                            && root.messageId.length > 0
                        text: "Context"
                        hoverEnabled: true
                        padding: 0
                        onClicked: root.contextInspectionRequested(root.messageId)
                        scale: down
                            ? root.theme.pressedScale
                            : hovered
                                ? root.theme.hoverScale
                                : 1.0

                        Behavior on scale {
                            NumberAnimation {
                                duration: root.theme.motionHover
                                easing.type: Easing.OutBack
                            }
                        }

                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered
                                ? root.theme.accentBright
                                : root.theme.mutedText
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered
                                ? root.theme.hoverBg
                                : "transparent"
                            border.width: 1
                            border.color: parent.hovered
                                ? "#554a7b"
                                : root.theme.quietBorder
                        }
                    }
                }
            }

            Item {
                id: surfaceFrame

                width: parent.width
                height: richContent.implicitHeight
                    + (root.userMessage || root.systemMessage ? 34 : 48)

                Rectangle {
                    x: 0
                    y: 4
                    width: parent.width
                    height: parent.height
                    radius: root.userMessage
                        ? root.theme.radiusMedium
                        : root.theme.radiusPanel
                    color: "#30000000"
                    visible: !root.systemMessage
                }

                Rectangle {
                    id: surface

                    anchors.fill: parent
                    radius: root.userMessage
                        ? root.theme.radiusMedium
                        : root.systemMessage
                            ? root.theme.radiusSmall
                            : root.theme.radiusPanel
                    color: root.userMessage
                        ? root.theme.userBg
                        : root.systemMessage
                            ? root.theme.systemBg
                            : root.theme.assistantBg
                    border.width: 1
                    border.color: root.failedMessage
                        ? root.theme.danger
                        : root.userMessage
                            ? root.theme.quietBorder
                            : root.systemMessage
                                ? root.theme.quietBorder
                                : root.theme.panelBorder
                    antialiasing: true
                    clip: true
                    opacity: root.streamingMessage ? 0.82 : 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 12
                        anchors.bottomMargin: 16
                        width: 2
                        radius: 1
                        color: root.failedMessage
                            ? root.theme.danger
                            : root.theme.accent
                        opacity: root.systemMessage || root.userMessage ? 0 : 0.72
                    }

                    RichMessageContent {
                        id: richContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: root.userMessage ? 18 : 26
                        anchors.rightMargin: root.userMessage ? 18 : 26
                        anchors.topMargin: root.userMessage ? 16 : 22
                        theme: root.theme
                        content: root.content
                        compact: root.userMessage || root.systemMessage
                    }
                }
            }
        }
    }
}
