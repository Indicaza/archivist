import QtQuick
import QtQuick.Controls
import "../../Icons"

// A small quiet control: an icon, maybe a label, maybe a menu caret.
// Home's toolbar buttons, filters and inline actions are all this.
Button {
    id: root

    required property var theme
    property string iconName: ""
    property string iconTone: ""
    property string label: ""
    property bool menu: false
    // A circle (icon only) instead of a pill.
    property bool round: false
    property bool selected: false
    property string toolTipText: ""
    property real iconSize: 14

    // The label's natural width; the label shrinks (elides) only when the
    // chip is given less room than that.
    readonly property real iconsWidth: (iconName.length > 0 ? Math.round(iconSize * 1) + (label.length > 0 ? 6 : 0) : 0)
        + (menu ? 15 : 0)

    implicitHeight: 32
    implicitWidth: round
        ? implicitHeight
        : Math.ceil(iconsWidth + (label.length > 0 ? metrics.advanceWidth + 1 : 0)) + leftPadding + rightPadding
    leftPadding: round ? 0 : (iconName.length > 0 ? 10 : 13)
    rightPadding: round ? 0 : (menu ? 9 : 13)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: label.length > 0 ? label : toolTipText
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.delay: 650
    ToolTip.text: toolTipText

    readonly property bool lit: hovered || activeFocus || selected || down

    TextMetrics {
        id: metrics

        text: root.label
        font: labelText.font
    }

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            spacing: 6

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconName.length > 0
                name: root.iconName
                tone: root.iconTone.length > 0 ? root.iconTone : root.lit ? "normal" : "muted"
                iconSize: root.iconSize
            }

            Text {
                id: labelText

                anchors.verticalCenter: parent.verticalCenter
                visible: root.label.length > 0
                width: Math.max(0, Math.min(metrics.advanceWidth + 1, root.width - root.leftPadding - root.rightPadding - root.iconsWidth))
                text: root.label
                elide: Text.ElideRight
                color: root.lit ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize + 1
                font.weight: root.selected ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
            }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.menu
                name: "chevron-down"
                tone: "muted"
                iconSize: 9
            }
        }
    }

    background: Rectangle {
        radius: height / 2
        color: root.down
            ? root.theme.activeBg
            : root.selected
                ? root.theme.withAlpha(root.theme.appText, 0.09)
                : root.hovered && root.enabled
                    ? root.theme.withAlpha(root.theme.appText, 0.06)
                    : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.theme.accent
        opacity: root.enabled ? 1 : 0.5

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}
