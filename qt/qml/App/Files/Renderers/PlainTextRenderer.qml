import QtQuick
import QtQuick.Controls

ScrollView {
    id: root

    required property var theme
    property string content: ""
    property bool showBorder: false

    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    TextArea {
        width: Math.max(root.availableWidth, implicitWidth)
        text: root.content
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.NoWrap
        textFormat: TextEdit.PlainText
        color: root.theme.appText
        selectionColor: root.theme.messageSelectionBg
        selectedTextColor: root.theme.messageSelectionText
        font.family: root.theme.monospaceFontFamily
        font.pixelSize: root.theme.typeCode
        leftPadding: 18
        rightPadding: 18
        topPadding: 16
        bottomPadding: 16

        background: Rectangle {
            color: "transparent"
            border.width: root.showBorder ? 1 : 0
            border.color: root.theme.quietBorder
        }
    }
}
