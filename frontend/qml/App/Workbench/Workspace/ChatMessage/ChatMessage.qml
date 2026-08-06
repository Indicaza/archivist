import QtQuick
import QtQuick.Controls
import "../ChatViewportPolicy.js" as ChatViewportPolicy

Item {
    id: root

    required property var theme
    required property string messageId
    required property string role
    required property string content
    required property string timestamp
    required property string status
    required property string progressLabel
    required property var activity
    required property var attachedFiles
    required property bool animateReveal
    required property real leftObstruction

    signal contextInspectionRequested(string messageId)
    signal revealProgressed()
    signal revealFinished(string messageId)

    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"
    readonly property bool providerWaiting: status === "streaming"
    readonly property bool waitingForFirstOutput: providerWaiting
        && content.trim().length === 0
    readonly property bool streamingMessage: providerWaiting
        || richContent.revealing
    readonly property bool failedMessage: status === "failed"
    readonly property bool cancelledMessage: status === "cancelled"
    readonly property string activityLabel: progressLabel.trim().length > 0
        ? progressLabel
        : "Working…"
    readonly property var contentGeometry: ChatViewportPolicy.contentZone(
        width,
        leftObstruction,
        theme.messageHorizontalInset,
        theme.transcriptContentWidth
    )
    readonly property real contentZoneX: Number(contentGeometry.x || 0)
    readonly property real contentZoneWidth: Number(contentGeometry.width || 0)
    readonly property real desiredFrameWidth: userMessage
        ? Math.min(
            theme.userMessageWidth,
            content.indexOf("\n") >= 0 || content.length > 76
                ? theme.userMessageWidth
                : Math.max(104, content.length * 8.4 + 48)
        )
        : theme.assistantMessageWidth

    width: ListView.view ? ListView.view.width : 900
    height: frame.height

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
            spacing: root.userMessage ? 0 : 14

            Item {
                visible: !root.userMessage
                width: parent.width
                height: visible ? 32 : 0

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Rectangle {
                        width: 22
                        height: 22
                        radius: 7
                        color: root.systemMessage ? "#1d1c19" : "#22201c"

                        Text {
                            anchors.centerIn: parent
                            text: root.systemMessage ? "!" : "✣"
                            color: root.theme.appText
                            font.pixelSize: root.theme.typeSize(11)
                            font.weight: Font.Bold
                            opacity: root.streamingMessage ? 0.62 : 1
                        }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Text {
                            text: root.systemMessage ? "SYSTEM" : "ARCHIVIST"
                            color: root.theme.appText
                            font.family: root.theme.chatFontFamily
                            font.pixelSize: root.theme.typeSize(10)
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                        }

                        Text {
                            text: root.failedMessage
                                ? "FAILED"
                                : root.cancelledMessage
                                    ? "STOPPED"
                                    : root.streamingMessage
                                        ? root.activityLabel
                                        : root.timestamp
                            color: root.failedMessage
                                ? root.theme.danger
                                : root.cancelledMessage
                                    ? root.theme.accentBright
                                    : root.streamingMessage
                                        ? root.theme.accentBright
                                        : root.theme.mutedText
                            font.family: root.theme.chatFontFamily
                            font.pixelSize: root.theme.typeSize(9)
                            opacity: root.failedMessage
                                ? 0.9
                                : root.cancelledMessage
                                    ? 0.72
                                    : root.streamingMessage
                                        ? 0.78
                                        : 0.52
                        }
                    }

                    Button {
                        id: contextButton

                        width: 64
                        height: 22
                        visible: !root.userMessage
                            && !root.systemMessage
                            && !root.streamingMessage
                            && !root.failedMessage
                            && !root.cancelledMessage
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
                            enabled: !contextButton.down

                            NumberAnimation {
                                duration: root.theme.motionHover
                                easing.type: Easing.OutCubic
                            }
                        }

                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered
                                ? root.theme.accentBright
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
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

            RunActivityCard {
                id: activityCard

                visible: root.waitingForFirstOutput
                    && !root.userMessage
                    && !root.systemMessage
                width: parent.width
                height: visible ? implicitHeight : 0
                opacity: visible ? 1 : 0
                theme: root.theme
                activity: root.activity
                attachedFiles: root.attachedFiles
                progressLabel: root.activityLabel

                Behavior on opacity {
                    NumberAnimation {
                        duration: root.theme.motionHover
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Item {
                id: surfaceFrame

                visible: !root.waitingForFirstOutput
                width: parent.width
                height: visible
                    ? (root.providerWaiting
                        ? streamingContent.contentHeight
                        : richContent.implicitHeight)
                        + (root.userMessage ? 30 : root.systemMessage ? 32 : 4)
                    : 0

                Rectangle {
                    x: 0
                    y: 3
                    width: parent.width
                    height: parent.height
                    radius: root.theme.radiusLarge
                    color: "#30000000"
                    visible: root.userMessage
                }

                Rectangle {
                    id: surface

                    anchors.fill: parent
                    radius: root.userMessage
                        ? root.theme.radiusLarge
                        : root.systemMessage
                            ? root.theme.radiusSmall
                            : 0
                    color: root.userMessage
                        ? root.theme.userBg
                        : root.systemMessage
                            ? root.theme.systemBg
                            : root.theme.assistantBg
                    border.width: root.userMessage || root.systemMessage ? 1 : 0
                    border.color: root.failedMessage
                        ? root.theme.danger
                        : root.cancelledMessage
                            ? root.theme.quietBorder
                            : root.userMessage
                                ? root.theme.quietBorder
                                : root.systemMessage
                                    ? root.theme.quietBorder
                                    : root.theme.panelBorder
                    antialiasing: true
                    clip: root.userMessage || root.systemMessage
                    opacity: root.providerWaiting ? 0.82 : 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        width: 2
                        radius: 1
                        color: root.failedMessage
                            ? root.theme.danger
                            : root.cancelledMessage
                                ? root.theme.accentBright
                                : root.theme.accent
                        opacity: root.failedMessage
                            ? 0.82
                            : root.cancelledMessage
                                ? 0.48
                                : 0
                    }

                    TextEdit {
                        id: streamingContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        visible: root.providerWaiting
                        height: visible ? contentHeight : 0
                        text: visible ? root.content : ""
                        color: root.theme.appText
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.typeBody
                        font.weight: Font.Normal
                        font.letterSpacing: 0.08
                        font.kerning: true
                        font.contextFontMerging: true
                        font.hintingPreference: Font.PreferNoHinting
                        font.preferTypoLineMetrics: true
                        textFormat: TextEdit.PlainText
                        textMargin: 0
                        wrapMode: TextEdit.Wrap
                        readOnly: true
                        selectByMouse: true
                        selectByKeyboard: true
                        persistentSelection: true
                        cursorVisible: false
                        selectionColor: root.theme.messageSelectionBg
                        selectedTextColor: root.theme.messageSelectionText
                        renderType: TextEdit.NativeRendering
                    }

                    RichMessageContent {
                        id: richContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: root.userMessage
                            ? 20
                            : root.systemMessage
                                ? 18
                                : 0
                        anchors.rightMargin: root.userMessage
                            ? 20
                            : root.systemMessage
                                ? 18
                                : 0
                        anchors.topMargin: root.userMessage
                            ? 14
                            : root.systemMessage
                                ? 16
                                : 0
                        visible: !root.providerWaiting
                        theme: root.theme
                        content: root.providerWaiting ? "" : root.content
                        compact: root.userMessage || root.systemMessage
                        animateReveal: root.animateReveal
                            && !root.userMessage
                            && !root.systemMessage
                        onRevealProgressed: root.revealProgressed()
                        onRevealFinished: root.revealFinished(root.messageId)
                    }
                }
            }
        }
    }
}
