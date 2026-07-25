import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../../Files/FileIdentity.js" as FileIdentity
import "../../../Files/RendererRegistry.js" as RendererRegistry
import "../../../Files/Renderers"

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

    readonly property var fileIdentity: FileIdentity.resolve({
        fileName: file && (file.name || file.relativePath)
            ? String(file.name || file.relativePath)
            : "",
        extension: file && file.extension ? String(file.extension) : "",
        mimeType: file && file.mimeType ? String(file.mimeType) : "",
        languageId: file && file.languageId ? String(file.languageId) : ""
    })
    readonly property var rendererSelection: RendererRegistry.resolve(fileIdentity)
    readonly property bool markdownRenderingAvailable: rendererSelection.id === "markdown"
        && rendererSelection.available
    readonly property bool imageRenderingAvailable: rendererSelection.id === "image"
        && rendererSelection.available
    readonly property bool directRenderingAvailable: markdownRenderingAvailable
        || imageRenderingAvailable
    readonly property bool displayLoading: loading && !imageRenderingAvailable
    readonly property string displayErrorMessage: imageRenderingAvailable
        ? ""
        : errorMessage
    readonly property bool displayContentAvailable: imageRenderingAvailable
        || content.length > 0
    readonly property string currentFileKey: String(file && file.id ? file.id : "")
        + ":"
        + String(rendererSelection.id || "plain-text")
    property string viewMode: "source"
    property real markdownZoom: 1.0
    property real imageZoom: 1.0
    property bool imageFitToView: true
    property bool imageCheckerboardVisible: true

    function setMarkdownZoom(value) {
        root.markdownZoom = Math.max(0.65, Math.min(2.0, value))
    }

    function zoomMarkdownIn() {
        root.setMarkdownZoom(root.markdownZoom + 0.1)
    }

    function zoomMarkdownOut() {
        root.setMarkdownZoom(root.markdownZoom - 0.1)
    }

    function resetMarkdownZoom() {
        root.markdownZoom = 1.0
    }

    function setImageZoom(value) {
        root.imageZoom = Math.max(0.05, Math.min(8.0, value))
    }

    function zoomImageIn() {
        root.setImageZoom(root.imageZoom + (root.imageZoom < 0.5 ? 0.05 : 0.1))
    }

    function zoomImageOut() {
        root.setImageZoom(root.imageZoom - (root.imageZoom <= 0.5 ? 0.05 : 0.1))
    }

    function fitImage() {
        root.imageFitToView = true
        root.imageZoom = 1.0
    }

    function showImageActualSize() {
        root.imageFitToView = false
        root.imageZoom = 1.0
    }

    function zoomActiveRendererIn() {
        if (root.imageRenderingAvailable) {
            root.zoomImageIn()
            return
        }

        root.zoomMarkdownIn()
    }

    function zoomActiveRendererOut() {
        if (root.imageRenderingAvailable) {
            root.zoomImageOut()
            return
        }

        root.zoomMarkdownOut()
    }

    function resetActiveRendererZoom() {
        if (root.imageRenderingAvailable) {
            root.showImageActualSize()
            return
        }

        root.resetMarkdownZoom()
    }

    function resetViewMode() {
        root.viewMode = root.directRenderingAvailable ? "rendered" : "source"
        root.markdownZoom = 1.0
        root.imageZoom = 1.0
        root.imageFitToView = true
    }

    onCurrentFileKeyChanged: Qt.callLater(root.resetViewMode)

    Component.onCompleted: resetViewMode()

    Shortcut {
        sequence: StandardKey.ZoomIn
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererIn()
    }

    Shortcut {
        sequence: StandardKey.ZoomOut
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererOut()
    }

    Shortcut {
        sequence: Qt.platform.os === "osx" ? "Meta+0" : "Ctrl+0"
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.resetActiveRendererZoom()
    }

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
                    text: root.fileIdentity.displayLabel.toUpperCase()
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.45
                }

                Text {
                    text: root.rendererSelection.displayLabel.toUpperCase()
                    color: root.rendererSelection.usedFallback
                        ? root.theme.mutedText
                        : root.theme.accentBright
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.45
                    opacity: root.rendererSelection.usedFallback ? 0.72 : 1
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
                    visible: !root.displayLoading
                        && root.displayErrorMessage.length === 0
                    text: root.imageRenderingAvailable
                        ? root.formattedSize(root.sizeBytes)
                        : root.formattedSize(root.sizeBytes)
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
            Layout.preferredHeight: root.directRenderingAvailable ? 36 : 0
            visible: root.directRenderingAvailable
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 6

                Item { Layout.fillWidth: true }

                Repeater {
                    visible: root.markdownRenderingAvailable
                    model: root.markdownRenderingAvailable ? [
                        { id: "rendered", label: "Rendered" },
                        { id: "source", label: "Source" },
                        { id: "split", label: "Split" }
                    ] : []

                    Button {
                        required property var modelData

                        Layout.preferredWidth: modelData.id === "rendered" ? 84 : 66
                        Layout.preferredHeight: 30
                        text: modelData.label
                        hoverEnabled: true
                        padding: 0
                        onClicked: root.viewMode = modelData.id

                        contentItem: Text {
                            text: parent.text
                            color: root.viewMode === modelData.id
                                ? root.theme.accentBright
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
                            font.weight: root.viewMode === modelData.id
                                ? Font.Bold
                                : Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: root.viewMode === modelData.id
                                ? root.theme.activeBg
                                : parent.hovered
                                    ? root.theme.hoverBg
                                    : root.theme.controlSurfaceBg
                            border.width: 1
                            border.color: root.viewMode === modelData.id
                                ? root.theme.accent
                                : root.theme.quietBorder
                            radius: 4
                        }
                    }
                }

                Button {
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 30
                    visible: root.imageRenderingAvailable
                    text: "Fit"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.fitImage()
                    ToolTip.visible: hovered
                    ToolTip.text: "Fit image to viewport"

                    contentItem: Text {
                        text: parent.text
                        color: root.imageFitToView
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: root.imageFitToView
                            ? root.theme.activeBg
                            : parent.hovered
                                ? root.theme.hoverBg
                                : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.imageFitToView
                            ? root.theme.accent
                            : root.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 30
                    visible: root.imageRenderingAvailable
                    text: "1:1"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.showImageActualSize()
                    ToolTip.visible: hovered
                    ToolTip.text: "Show actual pixel size"

                    contentItem: Text {
                        text: parent.text
                        color: !root.imageFitToView && root.imageZoom === 1.0
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: !root.imageFitToView && root.imageZoom === 1.0
                            ? root.theme.activeBg
                            : parent.hovered
                                ? root.theme.hoverBg
                                : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: !root.imageFitToView && root.imageZoom === 1.0
                            ? root.theme.accent
                            : root.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: 30
                    visible: root.imageRenderingAvailable
                    text: "Grid"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.imageCheckerboardVisible = !root.imageCheckerboardVisible
                    ToolTip.visible: hovered
                    ToolTip.text: root.imageCheckerboardVisible
                        ? "Hide transparency grid"
                        : "Show transparency grid"

                    contentItem: Text {
                        text: parent.text
                        color: root.imageCheckerboardVisible
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: root.imageCheckerboardVisible
                            ? root.theme.activeBg
                            : parent.hovered
                                ? root.theme.hoverBg
                                : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.imageCheckerboardVisible
                            ? root.theme.accent
                            : root.theme.quietBorder
                        radius: 4
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 22
                    visible: root.viewMode !== "source"
                    color: root.theme.quietBorder
                }

                Button {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    visible: root.viewMode !== "source"
                    text: "−"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.zoomActiveRendererOut()
                    ToolTip.visible: hovered
                    ToolTip.text: "Zoom out"

                    contentItem: Text {
                        text: parent.text
                        color: root.theme.appText
                        font.pixelSize: root.theme.typeSize(12)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? root.theme.hoverBg
                            : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 30
                    visible: root.viewMode !== "source"
                    text: root.imageRenderingAvailable
                        ? String(imageRenderer.effectivePercent) + "%"
                        : String(Math.round(root.markdownZoom * 100)) + "%"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.resetActiveRendererZoom()
                    ToolTip.visible: hovered
                    ToolTip.text: "Reset zoom"

                    contentItem: Text {
                        text: parent.text
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? root.theme.hoverBg
                            : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    visible: root.viewMode !== "source"
                    text: "+"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.zoomActiveRendererIn()
                    ToolTip.visible: hovered
                    ToolTip.text: "Zoom in"

                    contentItem: Text {
                        text: parent.text
                        color: root.theme.appText
                        font.pixelSize: root.theme.typeSize(11)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? root.theme.hoverBg
                            : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.theme.quietBorder
                        radius: 4
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

            PlainTextRenderer {
                anchors.fill: parent
                visible: !root.loading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                    && root.viewMode === "source"
                theme: root.theme
                content: root.content
            }

            MarkdownRenderer {
                anchors.fill: parent
                visible: !root.loading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                    && root.markdownRenderingAvailable
                    && root.viewMode === "rendered"
                theme: root.theme
                content: root.content
                libraryRootPath: String(LibraryStore.selectedLibrary.rootPath || "")
                documentRelativePath: String(root.file.relativePath || "")
                zoomFactor: root.markdownZoom
                onZoomFactorRequested: function(value) {
                    root.setMarkdownZoom(value)
                }
            }

            ImageRenderer {
                id: imageRenderer

                anchors.fill: parent
                visible: root.imageRenderingAvailable
                theme: root.theme
                libraryRootPath: String(LibraryStore.selectedLibrary.rootPath || "")
                relativePath: String(root.file && root.file.relativePath
                    ? root.file.relativePath
                    : "")
                zoomFactor: root.imageZoom
                fitToView: root.imageFitToView
                checkerboardVisible: root.imageCheckerboardVisible
                onZoomFactorRequested: function(value) {
                    root.setImageZoom(value)
                }
                onFitRequested: root.fitImage()
                onActualSizeRequested: root.showImageActualSize()
            }

            RowLayout {
                anchors.fill: parent
                visible: !root.loading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                    && root.markdownRenderingAvailable
                    && root.viewMode === "split"
                spacing: 0

                PlainTextRenderer {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 260
                    theme: root.theme
                    content: root.content
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: root.theme.quietBorder
                }

                MarkdownRenderer {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 320
                    theme: root.theme
                    content: root.content
                    libraryRootPath: String(LibraryStore.selectedLibrary.rootPath || "")
                    documentRelativePath: String(root.file.relativePath || "")
                    zoomFactor: root.markdownZoom
                    onZoomFactorRequested: function(value) {
                        root.setMarkdownZoom(value)
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 60)
                spacing: 8
                visible: root.displayLoading
                    || root.displayErrorMessage.length > 0
                    || !root.displayContentAvailable

                Text {
                    width: parent.width
                    text: root.displayLoading
                        ? "Opening file…"
                        : root.displayErrorMessage.length > 0
                            ? "File preview unavailable"
                            : "This file is empty"
                    color: root.displayErrorMessage.length > 0
                        ? root.theme.danger
                        : root.theme.appText
                    font.pixelSize: root.theme.typeSize(16)
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    visible: root.displayLoading
                        || root.displayErrorMessage.length > 0
                    text: root.displayLoading
                        ? "Archivist is validating and reading the cataloged file."
                        : root.displayErrorMessage
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
