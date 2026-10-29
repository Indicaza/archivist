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
    property string reason: ""
    // The panel's picture, cut from a grab of its window (see carry in
    // DockWorkspace).
    property url image: ""
    property rect imageRect: Qt.rect(0, 0, 0, 0)
    property size imageAreaSize: Qt.size(0, 0)
    readonly property bool hasImage: String(image).length > 0 && imageRect.width > 0

    // Drawn smaller than the real window, so the desktop stays visible.
    readonly property real previewScale: 0.5

    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput
    color: "transparent"
    width: Math.round(frame.width * previewScale) + 24
    height: Math.round(frame.height * previewScale) + 24
    // Where the pointer holds it, in the new window's coordinates: the
    // miniature keeps that spot under the pointer, as the window will.
    property point grab: Qt.point(pointer.x - frame.x, pointer.y - frame.y)
    x: Math.round(pointer.x - grab.x * previewScale - 12)
    y: Math.round(pointer.y - grab.y * previewScale - 12)
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

            // The panel itself, under the title bar.
            Item {
                id: picture

                x: 1
                y: 29
                width: parent.width - 2
                height: parent.height - 30
                clip: true
                visible: root.hasImage

                Image {
                    readonly property real sx: picture.width / Math.max(1, root.imageRect.width)
                    readonly property real sy: picture.height / Math.max(1, root.imageRect.height)

                    x: -root.imageRect.x * sx
                    y: -root.imageRect.y * sy
                    width: root.imageAreaSize.width * sx
                    height: root.imageAreaSize.height * sy
                    source: root.image
                    smooth: true
                    asynchronous: false
                    opacity: root.allowed ? 0.92 : 0.4
                }
            }

            Column {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 12
                visible: !root.hasImage || !root.allowed
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
                    text: root.allowed ? "Release to open in a new window"
                        : root.reason.length > 0 ? root.reason : "Already in its own window"
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
