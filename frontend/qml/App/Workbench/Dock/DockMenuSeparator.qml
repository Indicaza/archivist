import QtQuick
import QtQuick.Controls

MenuSeparator {
    id: root

    required property var theme

    implicitHeight: visible ? 9 : 0
    topPadding: 4
    bottomPadding: 4
    contentItem: Rectangle {
        implicitHeight: 1
        color: root.theme.quietBorder
    }
}
