import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A titled list on Home. Children go in the column under the title.
ColumnLayout {
    id: root

    required property var theme
    property string title: ""
    property string actionText: ""
    property string emptyText: ""
    property bool empty: false
    default property alias rows: rowColumn.data

    signal actionRequested()

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        Layout.leftMargin: 10
        Layout.rightMargin: 4
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: root.title.toUpperCase()
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightEmphasis
            font.letterSpacing: root.theme.textTrackingCaps
            verticalAlignment: Text.AlignVCenter
            Accessible.role: Accessible.Heading
        }

        Button {
            id: actionButton

            visible: root.actionText.length > 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            padding: 0
            leftPadding: 4
            rightPadding: 4
            Layout.preferredHeight: 22
            text: root.actionText
            onClicked: root.actionRequested()

            contentItem: Text {
                text: actionButton.text
                color: actionButton.hovered || actionButton.activeFocus
                    ? root.theme.accentBright
                    : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
                verticalAlignment: Text.AlignVCenter
            }

            background: Item {}
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        visible: root.empty && root.emptyText.length > 0
        text: root.emptyText
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textCaptionSize
        wrapMode: Text.Wrap
        lineHeight: root.theme.textLineHeightCompact
    }

    ColumnLayout {
        id: rowColumn

        Layout.fillWidth: true
        spacing: 1
    }
}
