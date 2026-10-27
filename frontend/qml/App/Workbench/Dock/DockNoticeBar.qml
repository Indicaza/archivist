import QtQuick
import QtQuick.Controls
import "../../Icons"

// A slim strip above a view's content for things that need a decision:
// recovered edits, a file that changed on disk. It slides open and closed.
Item {
    id: root

    required property var theme
    property bool open: false
    property string tone: "info" // info | warning
    property string iconName: tone === "warning" ? "warning" : "info"
    property string text: ""
    // Shown instead of `text` when the strip is narrow.
    property string shortText: text
    property string primaryText: ""
    property string secondaryText: ""
    property bool dismissible: false

    signal primaryClicked()
    signal secondaryClicked()
    signal dismissed()

    readonly property color toneColor: tone === "warning" ? "#d9a35b" : root.theme.accentBright

    clip: true
    height: open ? 34 : 0
    visible: height > 0.5

    Behavior on height {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 34
        color: Qt.rgba(root.toneColor.r, root.toneColor.g, root.toneColor.b, 0.1)

        Rectangle {
            width: 2
            height: parent.height
            color: root.toneColor
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        AppIcon {
            id: icon

            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            tone: root.tone === "warning" ? "warning" : "accent"
            iconSize: 13
        }

        Text {
            anchors.left: icon.right
            anchors.leftMargin: 8
            anchors.right: actions.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.width < 520 ? root.shortText : root.text
            elide: Text.ElideRight
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Row {
            id: actions

            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            DockDialogButton {
                visible: root.secondaryText.length > 0
                theme: root.theme
                implicitHeight: 24
                text: root.secondaryText
                onClicked: root.secondaryClicked()
            }

            DockDialogButton {
                visible: root.primaryText.length > 0
                theme: root.theme
                implicitHeight: 24
                primary: true
                text: root.primaryText
                onClicked: root.primaryClicked()
            }

            IconButton {
                visible: root.dismissible
                theme: root.theme
                width: 24
                height: 24
                iconName: "close"
                iconSize: 10
                toolTipText: "Dismiss"
                onClicked: root.dismissed()
            }
        }
    }
}
