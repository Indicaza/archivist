import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// Edit Home's toolbar: pick an arrangement, put back the default, finish.
// Showing, hiding, moving and resizing happen on the modules themselves.
Rectangle {
    id: root

    required property var theme

    signal doneRequested()

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 48
    radius: height / 2
    color: Qt.lighter(theme.homeCardBg, 1.12)
    border.width: 1
    border.color: theme.homeCardBorderHover

    // Shadow.
    Rectangle {
        z: -1
        x: 2
        y: 8
        width: parent.width - 4
        height: parent.height
        radius: parent.radius
        color: root.theme.withAlpha(root.theme.homeShadow, 0.35)
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 4

        Text {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 10
            rightPadding: 6
            text: "Arrange"
            color: root.theme.homeEyebrow
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Repeater {
            model: HomeStore.presets

            delegate: HomeChip {
                required property var modelData

                anchors.verticalCenter: parent.verticalCenter
                theme: root.theme
                label: String(modelData.title)
                selected: HomeStore.presetId === String(modelData.id)
                toolTipText: String(modelData.description)
                onClicked: HomeStore.applyPreset(String(modelData.id))
            }
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 20
            color: root.theme.homeCardBorderHover
        }

        HomeChip {
            anchors.verticalCenter: parent.verticalCenter
            theme: root.theme
            iconName: "refresh"
            iconSize: 12
            label: "Restore default"
            enabled: HomeStore.presetId !== "balanced"
            onClicked: HomeStore.restoreDefault()
        }

        Button {
            id: done

            anchors.verticalCenter: parent.verticalCenter
            height: 32
            leftPadding: 16
            rightPadding: 16
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            onClicked: root.doneRequested()

            contentItem: Text {
                text: "Done"
                color: "#1c1a16"
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize + 1
                font.weight: root.theme.textWeightEmphasis
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: height / 2
                color: done.down ? root.theme.homeSendPressedBg : done.hovered ? root.theme.homeSendHoverBg : root.theme.homeSendBg
                border.width: done.activeFocus ? 2 : 0
                border.color: root.theme.accentBright
            }
        }
    }
}
