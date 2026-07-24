import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Rectangle {
    id: root

    required property var theme
    required property var file
    required property var preview
    required property bool loading
    required property string errorMessage
    property real leftObstruction: 0
    property string pendingAttachmentFileId: ""

    readonly property string content: preview && preview.content
        ? String(preview.content)
        : ""
    readonly property int lineCount: preview && preview.lineCount !== undefined
        ? Number(preview.lineCount)
        : 0
    readonly property int sizeBytes: file && file.sizeBytes !== undefined
        ? Number(file.sizeBytes)
        : 0

    readonly property string attachmentId: attachmentIdForFile()
    readonly property bool attachedToChat: attachmentId.length > 0
    readonly property bool canAttach: Boolean(
        ChatStore.selectedChatId.length > 0
            && file
            && file.id
            && !loading
            && errorMessage.length === 0
            && !ChatStore.responding
            && !ChatStore.mutating
            && !ChatStore.mutatingAttachment
    )

    function attachmentIdForFile() {
        var attachments = ChatStore.attachments || []
        var fileId = root.file && root.file.id ? String(root.file.id) : ""

        for (var index = 0; index < attachments.length; index += 1) {
            if (String(attachments[index].fileId || "") === fileId) {
                return String(attachments[index].id || "")
            }
        }

        return ""
    }

    color: theme.workspaceBg
    clip: true

    Connections {
        target: ChatStore

        function onAttachmentAdded(attachment) {
            if (
                root.pendingAttachmentFileId.length > 0
                && String(attachment.fileId || "") === root.pendingAttachmentFileId
            ) {
                root.pendingAttachmentFileId = ""
                LibraryStore.clearFilePreview()
            }
        }

        function onErrorMessageChanged() {
            if (
                root.pendingAttachmentFileId.length > 0
                && !ChatStore.mutatingAttachment
                && ChatStore.errorMessage.length > 0
            ) {
                root.pendingAttachmentFileId = ""
            }
        }
    }

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
            Layout.preferredHeight: 46
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
                    font.pixelSize: root.theme.typeSize(9)
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
                    font.pixelSize: root.theme.typeSize(11)
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
                    font.pixelSize: root.theme.typeSize(9)
                    opacity: 0.72
                }


                Button {
                    id: attachmentButton

                    Layout.preferredWidth: root.attachedToChat ? 92 : 108
                    Layout.preferredHeight: 28
                    visible: !root.loading && root.errorMessage.length === 0
                    enabled: root.canAttach
                    text: root.pendingAttachmentFileId.length > 0
                        ? "Attaching…"
                        : root.attachedToChat
                            ? "✓  Attached"
                            : "＋  Attach to Chat"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: ChatStore.selectedChatId.length === 0
                        ? "Select a Chat before attaching this file"
                        : root.attachedToChat
                            ? "Remove this file from the selected Chat"
                            : "Use this file as explicit evidence in the selected Chat"
                    onClicked: {
                        if (root.attachedToChat) {
                            ChatStore.removeAttachment(root.attachmentId)
                        } else {
                            root.pendingAttachmentFileId = String(root.file.id)
                            ChatStore.attachFile(
                                LibraryStore.selectedLibraryId,
                                root.pendingAttachmentFileId
                            )
                        }
                    }
                    scale: down
                        ? root.theme.pressedScale
                        : hovered
                            ? root.theme.hoverScale
                            : 1.0

                    Behavior on scale {
                        enabled: !attachmentButton.down

                        NumberAnimation {
                            duration: root.theme.motionHover
                            easing.type: Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? root.attachedToChat
                                ? root.theme.accentBright
                                : root.theme.appText
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: parent.enabled ? 1 : 0.45
                    }

                    background: Rectangle {
                        color: parent.enabled && parent.hovered
                            ? root.theme.hoverBg
                            : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.attachedToChat
                            ? root.theme.accentBright
                            : root.theme.quietBorder
                        radius: 4
                        opacity: parent.enabled ? 1 : 0.55
                    }
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
                    selectionColor: root.theme.messageSelectionBg
                    selectedTextColor: root.theme.messageSelectionText
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "monospace"
                    font.pixelSize: root.theme.typeCode
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
                    font.pixelSize: root.theme.typeSize(16)
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
                    font.pixelSize: root.theme.typeSize(11)
                    lineHeight: root.theme.typeLineHeightBody
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
