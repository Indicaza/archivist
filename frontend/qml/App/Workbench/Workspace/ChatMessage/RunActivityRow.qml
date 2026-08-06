import QtQuick

Item {
    id: root

    required property var theme
    required property string label
    required property string detail
    property string rowState: "complete"
    property int entranceOrder: 0
    property bool shown: true

    readonly property bool active: rowState === "active"
    readonly property bool warning: rowState === "warning"

    width: parent ? parent.width : 480
    height: shown ? 23 : 0
    visible: shown

    function reveal() {
        if (!shown) {
            entrance.stop()
            return
        }

        rowBody.x = 8
        rowBody.opacity = 0
        entrance.restart()
    }

    onShownChanged: {
        if (shown) {
            revealTimer.restart()
            return
        }

        revealTimer.stop()
        entrance.stop()
        rowBody.opacity = 0
    }

    Component.onCompleted: revealTimer.start()

    Timer {
        id: revealTimer

        interval: 0
        repeat: false
        onTriggered: root.reveal()
    }

    Item {
        id: rowBody

        width: parent.width
        height: parent.height
        x: 8
        opacity: 0

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 3
            width: 1
            color: root.theme.quietBorder
            opacity: 0.68
        }

        Rectangle {
            id: rowDot

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: root.active ? 7 : 6
            height: width
            radius: width / 2
            color: root.warning
                ? root.theme.danger
                : root.active
                    ? root.theme.accentBright
                    : root.theme.accent
            border.width: root.rowState === "quiet" ? 1 : 0
            border.color: root.theme.quietBorder
            opacity: root.rowState === "quiet"
                ? 0.5
                : root.rowState === "evidence"
                    ? 0.62
                    : 0.88

            Behavior on color {
                ColorAnimation {
                    duration: root.theme.motionHover
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: 6.5
                color: "transparent"
                border.width: 1
                border.color: root.theme.accentBright
                visible: root.active
                opacity: 0.34

                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    running: root.active && root.shown

                    NumberAnimation {
                        from: 0.72
                        to: 1.08
                        duration: 560
                        easing.type: Easing.InOutSine
                    }

                    NumberAnimation {
                        from: 1.08
                        to: 0.72
                        duration: 560
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }

        Text {
            anchors.left: rowDot.right
            anchors.leftMargin: 10
            anchors.right: detailText.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.warning
                ? root.theme.danger
                : root.theme.appText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            font.weight: root.active ? Font.DemiBold : Font.Normal
            elide: Text.ElideMiddle
            opacity: root.rowState === "quiet" ? 0.62 : 0.9
        }

        Text {
            id: detailText

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(180, Math.max(82, parent.width * 0.34))
            text: root.detail
            color: root.warning
                ? root.theme.danger
                : root.active
                    ? root.theme.accentBright
                    : root.theme.mutedText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(8)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            opacity: root.active ? 0.82 : 0.58
        }
    }

    SequentialAnimation {
        id: entrance

        PauseAnimation {
            duration: Math.min(340, root.entranceOrder * 52)
        }

        ParallelAnimation {
            NumberAnimation {
                target: rowBody
                property: "x"
                from: 8
                to: 0
                duration: 180
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: rowBody
                property: "opacity"
                from: 0
                to: 1
                duration: 160
                easing.type: Easing.OutCubic
            }
        }
    }
}
