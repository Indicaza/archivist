import QtQuick
import QtQuick.Controls
import "../../Icons"

// Send, and Stop while a reply streams. A cream disc with a dark arrow
// when there's something to send; quiet when there isn't. Sending shows
// a turning ring, stopping a square.
Button {
    id: root

    required property var theme
    // idle | ready | sending | stop
    property string mode: "idle"

    signal sendRequested()
    signal stopRequested()

    readonly property bool lit: mode === "ready" || mode === "stop"

    implicitWidth: 36
    implicitHeight: 36
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    enabled: mode === "ready" || mode === "stop"
    Accessible.name: mode === "stop" ? "Stop the reply" : mode === "sending" ? "Sending" : "Send"
    ToolTip.visible: hovered
    ToolTip.delay: 600
    ToolTip.text: mode === "stop"
        ? "Stop the reply"
        : "Send (⏎). ⇧⏎ adds a line."
    onClicked: mode === "stop" ? stopRequested() : sendRequested()

    scale: down ? 0.92 : 1

    Behavior on scale {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
    }

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            name: "send"
            tone: root.lit ? "ink" : "muted"
            iconSize: 16
            opacity: root.mode === "ready" || root.mode === "idle" ? 1 : 0
            scale: opacity > 0.5 ? 1 : 0.6

            Behavior on opacity {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionFast }
            }
            Behavior on scale {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionFast; easing.type: Easing.OutBack }
            }
        }

        AppIcon {
            anchors.centerIn: parent
            name: "stop-square"
            tone: "ink"
            iconSize: 16
            opacity: root.mode === "stop" ? 1 : 0
            scale: opacity > 0.5 ? 1 : 0.6

            Behavior on opacity {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionFast }
            }
            Behavior on scale {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionFast; easing.type: Easing.OutBack }
            }
        }

        // Sending: a short arc turning inside the disc.
        Item {
            id: spinner

            anchors.centerIn: parent
            width: 16
            height: 16
            visible: root.mode === "sending"

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: root.theme.withAlpha(root.theme.mutedText, 0.25)
            }

            Rectangle {
                width: 4
                height: 4
                radius: 2
                x: parent.width / 2 - 2
                y: 0
                color: root.theme.appText
            }

            RotationAnimator on rotation {
                running: spinner.visible && !root.theme.reducedMotion
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
            }
        }
    }

    background: Rectangle {
        radius: height / 2
        color: root.lit
            ? (root.down ? root.theme.homeSendPressedBg : root.hovered ? root.theme.homeSendHoverBg : root.theme.homeSendBg)
            : root.theme.withAlpha(root.theme.appText, 0.07)
        border.width: root.activeFocus ? 2 : 0
        border.color: root.theme.accentBright

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}
