import QtQuick
import QtQuick.Controls

Button {
    id: root

    required property var theme
    property string iconName: "more"
    property string iconTone: enabled && (hovered || checked)
        ? "normal"
        : "muted"
    property real iconSize: 15
    property string toolTipText: ""
    property bool circular: false
    property color idleBackgroundColor: "transparent"
    property color hoverBackgroundColor: root.theme.hoverBg
    property color pressedBackgroundColor: root.theme.activeBg

    width: 32
    height: 32
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    spacing: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.name: toolTipText
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 350

    contentItem: Item {
        implicitWidth: 0
        implicitHeight: 0
    }

    background: Rectangle {
        id: buttonSurface
        radius: root.circular
            ? Math.min(width, height) / 2
            : root.theme.radiusSmall
        color: root.down
            ? root.pressedBackgroundColor
            : root.hovered || root.checked
                ? root.hoverBackgroundColor
                : root.idleBackgroundColor
        border.width: root.activeFocus || root.checked ? 1 : 0
        border.color: root.checked
            ? root.theme.accent
            : root.theme.panelBorder

        AppIcon {
            anchors.centerIn: parent
            name: root.iconName
            tone: root.iconTone
            iconSize: root.iconSize
            accessibleLabel: root.toolTipText
            opacity: root.enabled ? 1 : 0.42
        }
    }
}
