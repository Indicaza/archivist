import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property bool editing
    required property bool busy
    required property string collectionPath

    signal closeRequested()

    Layout.fillWidth: true
    Layout.preferredHeight: 76
    color: "#1a1916"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.quietBorder
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.right: closeButton.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Text {
            text: root.editing ? "COLLECTION MANAGEMENT" : "NEW COLLECTION"
            color: root.theme.accentBright
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }

        Text {
            width: parent.width
            text: root.editing
                ? root.collectionPath
                : "Organize a working world"
            color: root.theme.appText
            font.family: root.theme.titleFontFamily
            font.pixelSize: root.theme.typeSize(21)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    Button {
        id: closeButton

        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 34
        text: "×"
        enabled: !root.busy
        hoverEnabled: true
        padding: 0
        onClicked: root.closeRequested()

        contentItem: Text {
            text: parent.text
            color: parent.hovered ? root.theme.appText : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(18)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: parent.hovered ? root.theme.hoverBg : "transparent"
            radius: 4
        }
    }
}
