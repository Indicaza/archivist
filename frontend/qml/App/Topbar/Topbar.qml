import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../Icons"
import "../Workbench/Dock"

Rectangle {
    id: root

    required property var theme

    height: theme.topbarHeight
    color: theme.topbarBg
    border.width: 0
    z: 100

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.topbarBorder
    }

    readonly property bool dockingEnabled: !(typeof ArchivistDiagnostics !== "undefined"
        && Boolean(ArchivistDiagnostics.legacyWorkspace))

    // The layout switcher sits in the middle of the bar.
    Button {
        id: layoutButton

        anchors.centerIn: parent
        visible: root.dockingEnabled
        height: 28
        leftPadding: 10
        rightPadding: 8
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        z: 2
        ToolTip.visible: hovered && !layoutPicker.opened
        ToolTip.delay: 500
        ToolTip.text: "Layouts (Ctrl+Alt+L)"
        onClicked: layoutPicker.opened ? layoutPicker.close() : layoutPicker.open()

        contentItem: Row {
            spacing: 7

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "grid"
                tone: layoutButton.hovered || layoutPicker.opened ? "accent" : "muted"
                iconSize: 13
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Docking.currentLayoutName.length > 0 ? Docking.currentLayoutName : "Custom layout"
                color: layoutButton.hovered || layoutPicker.opened ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                tone: "muted"
                iconSize: 10
                rotation: layoutPicker.opened ? 180 : 0
                Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
        }

        background: Rectangle {
            radius: height / 2
            color: layoutButton.down || layoutPicker.opened ? root.theme.activeBg : layoutButton.hovered ? root.theme.hoverBg : "transparent"
            border.width: 1
            border.color: layoutPicker.opened ? root.theme.accent : root.theme.quietBorder

            Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
            Behavior on border.color { ColorAnimation { duration: root.theme.motionFast } }
        }

        DockLayoutPicker {
            id: layoutPicker

            parent: Overlay.overlay
            theme: root.theme
            x: Math.round((parent.width - width) / 2)
            y: root.height + 6
        }
    }

    Shortcut {
        enabled: root.dockingEnabled
        sequence: "Ctrl+Alt+L"
        context: Qt.ApplicationShortcut
        onActivated: layoutPicker.opened ? layoutPicker.close() : layoutPicker.open()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 31
        spacing: 8

        Text {
            Layout.preferredWidth: 146
            text: "Archivist"
            color: root.theme.appText
            font.family: root.theme.titleFontFamily
            font.pixelSize: root.theme.textTopbarBrandSize
            font.weight: root.theme.textWeightEmphasis
            verticalAlignment: Text.AlignVCenter
        }

        Item {
            Layout.fillWidth: true
        }

        IconButton {
            id: codeThemeButton

            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            theme: root.theme
            iconName: "settings"
            iconSize: 14
            toolTipText: "Code theme"
            checked: codeThemePicker.opened
            onClicked: codeThemePicker.opened
                ? codeThemePicker.close()
                : codeThemePicker.open()

            CodeThemePicker {
                id: codeThemePicker

                theme: root.theme
                x: codeThemeButton.width - width
                y: codeThemeButton.height + 8
            }
        }

        Rectangle {
            Layout.preferredWidth: root.theme.topbarAvatarSize
            Layout.preferredHeight: root.theme.topbarAvatarSize
            radius: width / 2
            border.width: 1
            border.color: "#3c3656"
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#9d8ae8" }
                GradientStop { position: 0.45; color: "#6f70ca" }
                GradientStop { position: 1.0; color: "#2b6f69" }
            }

            Rectangle {
                x: 6
                y: 4
                width: 7
                height: 5
                radius: 4
                color: "#70ffffff"
                rotation: -18
            }

            HoverHandler {
                id: avatarHover
            }

            scale: avatarHover.hovered ? 1.08 : 1.0

            Behavior on scale {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
