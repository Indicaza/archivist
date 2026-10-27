import QtQuick
import QtQuick.Controls

TextField {
    id: root

    required property var theme

    implicitHeight: 28
    color: theme.appText
    selectionColor: theme.accentSoft
    selectedTextColor: theme.appText
    placeholderTextColor: theme.mutedText
    font.family: theme.bodyFontFamily
    font.pixelSize: theme.typeSize(10)
    selectByMouse: true
    leftPadding: 8
    rightPadding: 8

    background: Rectangle {
        radius: 4
        color: root.theme.controlSurfaceBg
        border.width: 1
        border.color: root.activeFocus
            ? root.theme.accent
            : root.theme.quietBorder
    }
}
