import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var theme
    property bool open: false

    width: open ? theme.artifactDrawerWidth + 29 : 29
    height: Math.min(theme.artifactDrawerHeight, parent ? parent.height - 170 : theme.artifactDrawerHeight)

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

        contentItem: Text {
            text: parent.text
            color: parent.hovered ? root.theme.appText : root.theme.mutedText
            font.pixelSize: 18
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
        anchors.left: toggleButton.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: root.open
        radius: root.theme.radiusPanel
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        clip: true

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 32
                color: "#1a1916"

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
                    text: "✦  CHAT ARTIFACTS"
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }
            }

            Item {
                width: parent.width
                height: parent.height - 32

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 36
                    spacing: 10

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 50
                        height: 50
                        radius: 14
                        color: root.theme.accentSoft
                        border.width: 1
                        border.color: "#554a7b"

                        Text {
                            anchors.centerIn: parent
                            text: "✦"
                            color: root.theme.accentBright
                            font.pixelSize: 20
                        }
                    }

                    Text {
                        width: parent.width
                        text: "No artifacts yet"
                        color: root.theme.appText
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        width: parent.width
                        text: "Attachments, generated files, sources, and tool output will collect here."
                        color: root.theme.mutedText
                        font.pixelSize: 10
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    Behavior on width {
        NumberAnimation { duration: 190; easing.type: Easing.OutCubic }
    }
}
