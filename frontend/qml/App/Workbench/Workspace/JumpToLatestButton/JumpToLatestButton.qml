import QtQuick

// Round "↓" when you have scrolled up; a "New reply ↓" pill (or "3 new ↓")
// in the chat's hue when messages arrived while you were away.
Item {
    id: root

    required property var theme
    property color hue: theme.accent
    property int unreadCount: 0

    readonly property bool hasUnread: unreadCount > 0
    readonly property bool hovered: mouse.containsMouse
    readonly property bool down: mouse.pressed
    readonly property string label: unreadCount === 1
        ? "New reply"
        : unreadCount + " new"

    signal clicked()

    width: hasUnread ? pillText.implicitWidth + 44 : 32
    height: 32
    Accessible.role: Accessible.Button
    Accessible.name: hasUnread ? label + ", jump to latest" : "Jump to latest"

    Behavior on width {
        enabled: !root.theme.reducedMotion

        SpringAnimation {
            spring: root.theme.motionSpring
            damping: root.theme.motionDamping
            epsilon: 0.2
        }
    }

    scale: down
        ? root.theme.pressedScale
        : hovered
            ? root.theme.hoverScale
            : 1.0

    Behavior on scale {
        enabled: !root.down

        NumberAnimation {
            duration: root.theme.motionHover
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: -2
        radius: height / 2
        color: Qt.rgba(0, 0, 0, 0.28)
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.hasUnread
            ? Qt.tint(root.theme.controlSurfaceBg, root.theme.withAlpha(root.hue, root.hovered ? 0.34 : 0.24))
            : root.hovered
                ? "#292621"
                : "#201e1a"
        border.width: 1
        border.color: root.hasUnread
            ? root.theme.withAlpha(root.hue, 0.6)
            : root.theme.quietBorder

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }

    Text {
        id: pillText

        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        visible: root.hasUnread
        text: root.label
        color: root.theme.appText
        font.family: root.theme.chatFontFamily
        font.pixelSize: root.theme.typeSize(9)
        font.weight: Font.DemiBold
    }

    Text {
        id: arrow

        anchors.right: parent.right
        anchors.rightMargin: root.hasUnread ? 12 : 0
        anchors.left: root.hasUnread ? undefined : parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: "↓"
        color: root.hasUnread
            ? root.theme.agentHueBright(root.hue)
            : root.hovered
                ? root.theme.appText
                : root.theme.mutedText
        font.pixelSize: root.theme.typeSize(15)
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
    }

    // A small hop when a new message arrives.
    SequentialAnimation {
        id: nudge

        NumberAnimation { target: arrow; property: "anchors.verticalCenterOffset"; to: 3; duration: 110; easing.type: Easing.OutCubic }
        SpringAnimation { target: arrow; property: "anchors.verticalCenterOffset"; to: 0; spring: 4; damping: 0.22; epsilon: 0.05 }
    }

    onUnreadCountChanged: {
        if (unreadCount > 0 && !root.theme.reducedMotion) {
            nudge.restart()
        }
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
