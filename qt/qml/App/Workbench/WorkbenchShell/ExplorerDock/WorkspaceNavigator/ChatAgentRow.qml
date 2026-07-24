import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var theme
    required property var agent
    required property bool active
    required property bool busy

    signal activateRequested(string agentId)

    height: 27
    radius: 4
    color: agentTap.pressed
        ? "#292621"
        : agentHover.hovered
            ? root.theme.hoverBg
            : root.active
                ? "#211f1c"
                : "transparent"
    border.width: root.active ? 1 : 0
    border.color: "#554a7b"

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 5
        height: 5
        radius: 3
        color: root.active
            ? root.theme.accentBright
            : root.theme.mutedText
        opacity: root.active ? 1 : 0.58
    }

    Text {
        anchors.left: parent.left
        anchors.right: activeLabel.left
        anchors.leftMargin: agentHover.hovered ? 23 : 20
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: String(root.agent.name || "Unnamed Agent")
        color: root.active || agentHover.hovered
            ? root.theme.appText
            : root.theme.mutedText
        font.pixelSize: root.theme.typeSize(8)
        font.weight: root.active ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight

    }

    Text {
        id: activeLabel

        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: root.active
        text: "ACTIVE"
        color: root.theme.accentBright
        font.pixelSize: root.theme.typeSize(7)
        font.weight: Font.Bold
        font.letterSpacing: 0.45
    }

    HoverHandler {
        id: agentHover
    }

    TapHandler {
        id: agentTap

        enabled: !root.busy && !root.active
        onTapped: root.activateRequested(String(root.agent.id || ""))
    }
}
