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

    width: 32
    height: 32
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.name: toolTipText
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 350

    contentItem: AppIcon {
        anchors.centerIn: parent
        name: root.iconName
        tone: root.iconTone
        iconSize: root.iconSize
        accessibleLabel: root.toolTipText
        opacity: root.enabled ? 1 : 0.42
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.down
            ? root.theme.activeBg
            : root.hovered || root.checked
                ? root.theme.hoverBg
                : "transparent"
        border.width: root.activeFocus || root.checked ? 1 : 0
        border.color: root.checked
            ? root.theme.accent
            : root.theme.panelBorder
    }
}
