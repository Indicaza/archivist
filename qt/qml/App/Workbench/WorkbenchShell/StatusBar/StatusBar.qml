import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property bool filePreviewActive
    property string fileAccessLabel: ""
    property string fileTypeLabel: ""
    property string filePath: ""
    property string fileMetrics: ""
    property bool fileAttached: false

    function compactTypeLabel(value) {
        var label = String(value || "").trim().toUpperCase()
        var suffixes = [
            " SPREADSHEET",
            " PRESENTATION",
            " DOCUMENT",
            " IMAGE",
            " FILE"
        ]

        for (
            var index = 0;
            index < suffixes.length;
            index += 1
        ) {
            var suffix = suffixes[index]

            if (
                label.length > suffix.length
                && label.lastIndexOf(suffix)
                    === label.length - suffix.length
            ) {
                return label.slice(
                    0,
                    label.length - suffix.length
                )
            }
        }

        return label
    }

    readonly property string compactFileType:
        compactTypeLabel(fileTypeLabel)

    height: theme.statusBarHeight
    color: theme.workspaceBgDeep
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.quietBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10
        spacing: 7

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.theme.success
        }

        Text {
            text: "Local"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.DemiBold
        }

        Rectangle {
            visible: root.filePreviewActive
            Layout.preferredWidth: 1
            Layout.preferredHeight: 12
            color: root.theme.quietBorder
        }

        Text {
            visible: root.filePreviewActive
            text: [
                root.fileAccessLabel,
                root.compactFileType,
                root.fileMetrics
            ].filter(function(value) {
                return String(value || "").length > 0
            }).join("  ·  ")
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(8)
            font.weight: Font.DemiBold
        }

        Text {
            visible: root.filePreviewActive
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            text: root.filePath
            color: root.theme.appText
            font.pixelSize: root.theme.typeSize(9)
            elide: Text.ElideMiddle
            opacity: 0.84
        }

        Text {
            visible: root.filePreviewActive
                && root.fileAttached
            text: "✓ Attached"
            color: root.theme.accentBright
            font.pixelSize: root.theme.typeSize(8)
            font.weight: Font.DemiBold
        }

        Item {
            visible: !root.filePreviewActive
            Layout.fillWidth: true
        }
    }
}
