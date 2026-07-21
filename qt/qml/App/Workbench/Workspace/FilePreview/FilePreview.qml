import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var file
    required property var preview
    required property bool loading
    required property string errorMessage
    property real leftObstruction: 0

    readonly property string content: preview && preview.content
        ? String(preview.content)
        : ""
    readonly property int lineCount: preview && preview.lineCount !== undefined
        ? Number(preview.lineCount)
        : 0
    readonly property int sizeBytes: file && file.sizeBytes !== undefined
        ? Number(file.sizeBytes)
        : 0

    color: theme.workspaceBg
    clip: true

    function formattedSize(bytes) {
        if (bytes < 1024) {
            return String(bytes) + " B"
        }

        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0) + " KB"
        }

        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: Math.max(18, root.leftObstruction + 18)
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.quietBorder
            radius: 5

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Text {
                    text: "READ-ONLY"
                    color: root.theme.accentBright
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 0.65
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 16
                    color: root.theme.quietBorder
                }

                Text {
                    Layout.fillWidth: true
                    text: root.file && root.file.relativePath
                        ? String(root.file.relativePath)
                        : "Library file"
                    color: root.theme.appText
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Text {
                    visible: !root.loading && root.errorMessage.length === 0
                    text: root.formattedSize(root.sizeBytes)
                        + "  ·  "
                        + String(root.lineCount)
                        + (root.lineCount === 1 ? " line" : " lines")
                    color: root.theme.mutedText
                    font.pixelSize: 8
                    opacity: 0.72
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.theme.workspaceBgDeep
            border.width: 1
            border.color: root.theme.quietBorder
            radius: 5
            clip: true

            ScrollView {
                id: previewScroll

                anchors.fill: parent
                visible: !root.loading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: previewText

                    width: Math.max(previewScroll.availableWidth, implicitWidth)
                    text: root.content
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    color: root.theme.appText
                    selectionColor: "#5a4d8c"
                    selectedTextColor: "#ffffff"
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "monospace"
                    font.pixelSize: 11
                    leftPadding: 18
                    rightPadding: 18
                    topPadding: 16
                    bottomPadding: 16

                    background: Rectangle {
                        color: "transparent"
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 60)
                spacing: 8
                visible: root.loading
                    || root.errorMessage.length > 0
                    || root.content.length === 0

                Text {
                    width: parent.width
                    text: root.loading
                        ? "Opening file…"
                        : root.errorMessage.length > 0
                            ? "File preview unavailable"
                            : "This file is empty"
                    color: root.errorMessage.length > 0
                        ? root.theme.danger
                        : root.theme.appText
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    visible: root.loading || root.errorMessage.length > 0
                    text: root.loading
                        ? "Archivist is validating and reading the cataloged file."
                        : root.errorMessage
                    color: root.theme.mutedText
                    font.pixelSize: 10
                    lineHeight: 1.45
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
