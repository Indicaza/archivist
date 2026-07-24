import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "ContextInspector"

Item {
    id: root

    required property var theme
    property bool open: false

    function openForMessage(messageId) {
        root.open = true
        ChatStore.loadMessageContext(messageId)
    }

    width: open ? theme.artifactDrawerWidth + 29 : 29
    height: Math.min(
        theme.artifactDrawerHeight,
        parent ? parent.height - 96 : theme.artifactDrawerHeight
    )

    Button {
        id: toggleButton

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 29
        height: 42
        text: root.open ? "›" : "‹"
        hoverEnabled: true
        padding: 0
        onClicked: root.open = !root.open
        ToolTip.visible: hovered
        ToolTip.text: root.open ? "Close Context Inspector" : "Open Context Inspector"
        scale: down
            ? root.theme.pressedScale
            : hovered
                ? root.theme.hoverScale
                : 1.0

        Behavior on scale {
            enabled: !toggleButton.down

            NumberAnimation {
                duration: root.theme.motionHover
                easing.type: Easing.OutCubic
            }
        }

        contentItem: Text {
            text: parent.text
            color: parent.hovered ? root.theme.appText : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(18)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: root.open ? 0 : 7
            color: parent.hovered ? root.theme.hoverBg : root.theme.surfaceBg
            border.width: 1
            border.color: parent.hovered ? "#554a7b" : root.theme.panelBorder
        }
    }

    Rectangle {
        id: inspectorPanel

        anchors.left: toggleButton.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        opacity: root.open ? 1.0 : 0.0
        scale: root.open ? 1.0 : root.theme.modalSpawnScale
        transformOrigin: Item.Right
        radius: root.theme.radiusPanel
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        clip: true

        Behavior on opacity {
            NumberAnimation {
                duration: root.open
                    ? root.theme.motionModalLaunch
                    : root.theme.motionModalClose
                easing.type: root.open ? Easing.OutCubic : Easing.InCubic
            }
        }

        Behavior on scale {
            NumberAnimation {
                duration: root.open
                    ? root.theme.motionModalLaunch + root.theme.motionModalSettle
                    : root.theme.motionModalCloseKick
                        + root.theme.motionModalClose
                easing.type: root.open ? Easing.OutBack : Easing.InBack
                easing.overshoot: 1.05
            }
        }

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 40
                color: root.theme.controlSurfaceBg

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: root.theme.quietBorder
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "✦  CONTEXT INSPECTOR"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }

                Button {
                    id: closeButton

                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    text: "×"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.open = false
                    scale: down
                        ? root.theme.pressedScale
                        : hovered
                            ? root.theme.hoverScale
                            : 1.0

                    Behavior on scale {
                        enabled: !closeButton.down

                        NumberAnimation {
                            duration: root.theme.motionHover
                            easing.type: Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered
                            ? root.theme.appText
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(14)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                    }
                }
            }

            ContextInspector {
                width: parent.width
                height: parent.height - 40
                theme: root.theme
                onSourceOpening: root.open = false
            }
        }
    }

    Behavior on width {
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.OutCubic
        }
    }
}
