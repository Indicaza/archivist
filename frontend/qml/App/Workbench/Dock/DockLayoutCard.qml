import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One layout in the picker: a live miniature, its name and what it is for.
Item {
    id: root

    required property var theme
    property string layoutId: ""
    property string name: ""
    property string description: ""
    property bool active: false
    property bool editable: false
    property int previewRevision: 0
    property bool renaming: false

    signal chosen()
    signal renamed(string name)
    signal deleted()

    implicitWidth: 164
    implicitHeight: thumb.height + 8 + titleRow.height + (descriptionText.visible ? descriptionText.height + 2 : 0) + 6

    readonly property bool hovered: cardHover.hovered

    HoverHandler {
        id: cardHover
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -5
        radius: 9
        color: root.hovered ? root.theme.hoverBg : "transparent"

        Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
    }

    DockLayoutThumb {
        id: thumb

        width: parent.width
        height: Math.round(width * 0.6)
        theme: root.theme
        active: root.active
        hovered: root.hovered
        rects: {
            root.previewRevision
            return Docking.layoutPreview(root.layoutId)
        }
        scale: tapArea.pressed ? 0.97 : root.hovered ? 1.02 : 1
        Behavior on scale { enabled: !root.theme.reducedMotion; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        Rectangle {
            visible: root.active
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: -5
            width: 18
            height: 18
            radius: 9
            color: root.theme.accentBright

            AppIcon {
                anchors.centerIn: parent
                name: "check"
                tone: "normal"
                iconSize: 10
            }
        }
    }

    MouseArea {
        id: tapArea

        anchors.fill: thumb
        cursorShape: Qt.PointingHandCursor
        onClicked: root.chosen()
    }

    Item {
        id: titleRow

        anchors.top: thumb.bottom
        anchors.topMargin: 8
        width: parent.width
        height: 20

        Text {
            visible: !root.renaming
            anchors.left: parent.left
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            elide: Text.ElideRight
            color: root.active ? root.theme.accentBright : root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize + 1
            font.weight: root.theme.textWeightEmphasis

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.chosen()
                onDoubleClicked: if (root.editable) root.renaming = true
            }
        }

        TextField {
            id: renameField

            visible: root.renaming
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 24
            text: root.name
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            leftPadding: 6
            selectByMouse: true
            background: Rectangle {
                radius: 4
                color: root.theme.workspaceBgDeep
                border.width: 1
                border.color: root.theme.accent
            }
            onVisibleChanged: if (visible) { text = root.name; selectAll(); forceActiveFocus() }
            onAccepted: { root.renamed(text); root.renaming = false }
            Keys.onEscapePressed: root.renaming = false
            onActiveFocusChanged: if (!activeFocus) root.renaming = false
        }

        Row {
            id: actions

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.editable && !root.renaming
            opacity: root.hovered ? 1 : 0
            spacing: 0

            Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }

            IconButton {
                theme: root.theme
                width: 20
                height: 20
                iconName: "edit"
                iconSize: 11
                toolTipText: "Rename"
                onClicked: root.renaming = true
            }

            IconButton {
                theme: root.theme
                width: 20
                height: 20
                iconName: "close"
                iconSize: 10
                toolTipText: "Delete layout"
                onClicked: root.deleted()
            }
        }
    }

    Text {
        id: descriptionText

        anchors.top: titleRow.bottom
        anchors.topMargin: 2
        width: parent.width
        visible: root.description.length > 0
        text: root.description
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textMetadataSize
        lineHeight: 1.1
    }
}
