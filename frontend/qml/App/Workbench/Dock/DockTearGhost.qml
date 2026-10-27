import QtQuick
import QtQuick.Window
import "../../Icons"

// While a tab or panel is dragged out past every window, this outline of
// the window it would become follows the pointer. Releasing opens it there.
Window {
    id: root

    required property var theme
    property string title: ""
    property string icon: "file"
    property bool shown: false
    property bool allowed: true
    property point pointer: Qt.point(0, 0)
    // Where the new window would open (global coordinates).
    property rect frame: Qt.rect(pointer.x - 60, pointer.y - 14, sourceSize.width, sourceSize.height)
    property size sourceSize: Qt.size(720, 480)

    // Drawn smaller than the real window, so the desktop stays visible.
    readonly property real previewScale: 0.5

    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput
    color: "transparent"
    width: Math.round(frame.width * previewScale) + 24
    height: Math.round(frame.height * previewScale) + 24
    // The miniature's corner sits on the new window's corner.
    x: Math.round(frame.x - 12)
    y: Math.round(frame.y - 12)
    visible: shown

    Item {
        id: card

        x: 12
        y: 12
        width: root.width - 24
        height: root.height - 24
        opacity: root.shown ? 1 : 0
        scale: root.shown ? 1 : 0.9
        transformOrigin: Item.TopLeft

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 120 } }
        Behavior on scale { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 200; easing.type: Easing.OutBack } }

        Rectangle {
            // Shadow.
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.leftMargin: 3
            anchors.rightMargin: -3
            anchors.bottomMargin: -8
            radius: 12
            color: "#000000"
            opacity: 0.28
        }

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: Qt.rgba(root.theme.workspaceBgDeep.r, root.theme.workspaceBgDeep.g, root.theme.workspaceBgDeep.b, 0.92)
            border.width: 1.5
            border.color: root.allowed ? root.theme.accentBright : root.theme.danger

            // Title bar of the window-to-be.
            Rectangle {
                x: 1
                y: 1
                width: parent.width - 2
                height: 28
                radius: 9
                color: root.theme.controlSurfaceBg

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 9
                    color: parent.color
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    spacing: 7

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: root.icon
                        tone: root.allowed ? "accent" : "danger"
                        iconSize: 13
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, card.width - 48)
                        text: root.title
                        elide: Text.ElideRight
                        color: root.theme.appText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textTabSize
                        font.weight: root.theme.textWeightEmphasis
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 12
                spacing: 6
                width: parent.width - 24

                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: root.allowed ? "external-link" : "close"
                    tone: root.allowed ? "accent" : "danger"
                    iconSize: 18
                    visible: card.height > 110
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.allowed ? "Release to open in a new window" : "Already in its own window"
                    wrapMode: Text.WordWrap
                    color: root.allowed ? root.theme.appText : root.theme.danger
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    font.weight: root.theme.textWeightEmphasis
                }
            }
        }
    }
}
