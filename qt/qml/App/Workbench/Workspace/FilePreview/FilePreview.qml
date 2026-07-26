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
    readonly property bool documentRenderingAvailable: rendererSelection.id === "pdf"
        && rendererSelection.available
    readonly property bool directRenderingAvailable: markdownRenderingAvailable
        || imageRenderingAvailable
        || documentRenderingAvailable
    readonly property bool displayLoading: documentRenderingAvailable
        ? documentPreview.state === "loading"
        : loading && !imageRenderingAvailable
    readonly property string displayErrorMessage: documentRenderingAvailable
        ? documentPreview.errorMessage
        : imageRenderingAvailable
            ? ""
            : errorMessage
    readonly property bool displayContentAvailable: imageRenderingAvailable
        || (documentRenderingAvailable && documentPreview.state === "ready")
        || content.length > 0
    readonly property string currentFileKey: String(file && file.id ? file.id : "")
        + ":"
        + String(rendererSelection.id || "plain-text")
    property string viewMode: "source"
    property real markdownZoom: 1.0
    property real imageZoom: 1.0
    property bool imageFitToView: false
    property bool imageCheckerboardVisible: true
    property real documentZoom: 1.0
    property bool documentFitToWidth: false
    property string activeViewportStateKey: ""
    property var pendingViewportState: ({})
    property var cachedViewportState: ({})
    property bool restoringViewportState: false
    property int viewportRestorePass: 0

    readonly property string viewportStateKey: (
        String(CollectionStore.selectedCollectionId || "").length > 0
        && String(LibraryStore.selectedLibraryId || "").length > 0
        && String(file && file.id ? file.id : "").length > 0
    )
        ? "workspace/collections/"
            + String(CollectionStore.selectedCollectionId)
            + "/viewports/files/"
            + String(LibraryStore.selectedLibraryId)
            + "/"
            + String(file.id)
        : ""

    function numberValue(value, fallback) {
        var number = Number(value)
        return isFinite(number) ? number : Number(fallback || 0)
    }

    function defaultViewportState() {
        return {
            version: 2,
            viewMode: root.directRenderingAvailable ? "rendered" : "source",
            markdownZoom: 1.0,
            imageZoom: 1.0,
            imageFitToView: false,
            imageCheckerboardVisible: true,
            documentZoom: 1.0,
            documentFitToWidth: false,
            sourceX: 0,
            sourceY: 0,
            sourceXRatio: 0,
            sourceYRatio: 0,
            markdownX: 0,
            markdownY: 0,
            markdownXRatio: 0,
            markdownYRatio: 0,
            imageViewportVersion: 1,
            imageX: 0,
            imageY: 0,
            imageFocusX: 0.5,
            imageFocusY: 0.5,
            documentPage: 0,
            documentViewportVersion: 5,
            documentX: 0,
            documentY: 0,
            documentFocusX: 0.5,
            documentFocusY: 0.5,
            splitSourceX: 0,
            splitSourceY: 0,
            splitSourceXRatio: 0,
            splitSourceYRatio: 0,
            splitMarkdownX: 0,
            splitMarkdownY: 0,
            splitMarkdownXRatio: 0,
            splitMarkdownYRatio: 0
        }
    }

    function mergedViewportState(patch) {
        var next = ({})
        var source = root.cachedViewportState
            && typeof root.cachedViewportState === "object"
            ? root.cachedViewportState
            : root.defaultViewportState()

        for (var sourceKey in source) {
            next[sourceKey] = source[sourceKey]
        }

        var changes = patch || ({})
        for (var changeKey in changes) {
            next[changeKey] = changes[changeKey]
        }

        next.version = 2
        return next
    }

    function updateViewportState(patch) {
        if (
            root.restoringViewportState
            || root.activeViewportStateKey.length === 0
        ) {
            return
        }

        root.cachedViewportState = root.mergedViewportState(patch)
        viewportStateSaveTimer.restart()
    }

    function saveViewportState(stateKey) {
        var key = String(stateKey || root.activeViewportStateKey || "")

        if (key.length === 0) {
            return
        }

        var state = root.cachedViewportState

        if (!state || typeof state !== "object") {
            state = root.defaultViewportState()
        }

        WorkspaceState.setValue(
            key,
            JSON.stringify(state)
        )
    }

    function restoreViewportState(stateKey) {
        viewportStateSaveTimer.stop()

        if (root.activeViewportStateKey.length > 0) {
            root.saveViewportState(root.activeViewportStateKey)
        }

        root.activeViewportStateKey = String(stateKey || "")
        root.restoringViewportState = true
        root.viewportRestorePass = 0

        var state = root.defaultViewportState()

        if (root.activeViewportStateKey.length > 0) {
            var raw = String(
                WorkspaceState.value(
                    root.activeViewportStateKey,
                    ""
                ) || ""
            )

            if (raw.length > 0) {
                try {
                    var parsed = JSON.parse(raw)
                    if (parsed && typeof parsed === "object") {
                        for (var key in parsed) {
                            state[key] = parsed[key]
                        }
                    }
                } catch (error) {
                    state = root.defaultViewportState()
                }
            }
        }

        root.pendingViewportState = state
        root.cachedViewportState = state
        root.viewMode = String(
            state.viewMode || root.defaultViewportState().viewMode
        )
        root.markdownZoom = Math.max(
            0.65,
            Math.min(2.0, root.numberValue(state.markdownZoom, 1.0))
        )
        root.imageZoom = Math.max(
            0.05,
            Math.min(8.0, root.numberValue(state.imageZoom, 1.0))
        )
        root.imageFitToView = Boolean(state.imageFitToView)
        root.imageCheckerboardVisible =
            state.imageCheckerboardVisible === undefined
                ? true
                : Boolean(state.imageCheckerboardVisible)
        root.documentZoom = Math.max(
            0.2,
            Math.min(6.0, root.numberValue(state.documentZoom, 1.0))
        )
        root.documentFitToWidth = Boolean(state.documentFitToWidth)

        viewportStateRestoreTimer.restart()
    }

    function restoreVisibleViewport() {
        var state = root.cachedViewportState || root.defaultViewportState()

        if (root.viewMode === "source") {
            sourceRenderer.restoreViewport(
                root.numberValue(state.sourceX, 0),
                root.numberValue(state.sourceY, 0),
                root.numberValue(state.sourceXRatio, 0),
                root.numberValue(state.sourceYRatio, 0)
            )
            return
        }

        if (root.viewMode === "split") {
            splitSourceRenderer.restoreViewport(
                root.numberValue(state.splitSourceX, 0),
                root.numberValue(state.splitSourceY, 0),
                root.numberValue(state.splitSourceXRatio, 0),
                root.numberValue(state.splitSourceYRatio, 0)
            )
            splitMarkdownRenderer.restoreViewport(
                root.numberValue(state.splitMarkdownX, 0),
                root.numberValue(state.splitMarkdownY, 0),
                root.numberValue(state.splitMarkdownXRatio, 0),
                root.numberValue(state.splitMarkdownYRatio, 0)
            )
            return
        }

        if (root.markdownRenderingAvailable) {
            markdownRenderer.restoreViewport(
                root.numberValue(state.markdownX, 0),
                root.numberValue(state.markdownY, 0),
                root.numberValue(state.markdownXRatio, 0),
                root.numberValue(state.markdownYRatio, 0)
            )
        }

        if (root.imageRenderingAvailable) {
            var imageViewportVersion = Math.max(
                0,
                Math.round(
                    root.numberValue(
                        state.imageViewportVersion,
                        0
                    )
                )
            )

            imageRenderer.restoreViewport(
                root.numberValue(state.imageX, 0),
                root.numberValue(state.imageY, 0),
                imageViewportVersion >= 1
                    ? root.numberValue(state.imageFocusX, 0.5)
                    : 0.5,
                imageViewportVersion >= 1
                    ? root.numberValue(state.imageFocusY, 0.5)
                    : 0.5
            )
        }

        if (root.documentRenderingAvailable) {
            var documentViewportVersion = Math.max(
                0,
                Math.round(
                    root.numberValue(
                        state.documentViewportVersion,
                        0
                    )
                )
            )

            pdfRenderer.restoreViewport(
                Math.max(
                    0,
                    Math.round(root.numberValue(state.documentPage, 0))
                ),
                root.numberValue(state.documentZoom, 1.0),
                Boolean(state.documentFitToWidth),
                documentViewportVersion >= 5
                    ? root.numberValue(state.documentX, 0)
                    : 0,
                documentViewportVersion >= 5
                    ? root.numberValue(state.documentY, 0)
                    : 0,
                documentViewportVersion >= 5
                    ? root.numberValue(state.documentFocusX, 0.5)
                    : 0.5,
                documentViewportVersion >= 5
                    ? root.numberValue(state.documentFocusY, 0.5)
                    : 0.5
            )
        }
    }

    function applyPendingViewportState() {
        root.restoreVisibleViewport()
        root.viewportRestorePass += 1

        if (root.viewportRestorePass < 5) {
            viewportStateRestoreTimer.restart()
        } else {
            root.restoringViewportState = false
        }
    }

    function setMarkdownZoom(value) {
        root.markdownZoom = Math.max(0.65, Math.min(2.0, value))
        root.updateViewportState({
            markdownZoom: root.markdownZoom
        })
    }

    function zoomMarkdownIn() {
        root.setMarkdownZoom(root.markdownZoom + 0.1)
    }

    function zoomMarkdownOut() {
        root.setMarkdownZoom(root.markdownZoom - 0.1)
    }

    function resetMarkdownZoom() {
        root.setMarkdownZoom(1.0)
    }

    function setImageZoom(value, focusX, focusY) {
        root.imageZoom = Math.max(0.05, Math.min(8.0, value))
        root.updateViewportState({
            imageZoom: root.imageZoom,
            imageFocusX: root.numberValue(focusX, 0.5),
            imageFocusY: root.numberValue(focusY, 0.5)
        })
    }

    function zoomImageIn() {
        var currentZoom = imageRenderer.effectiveScale
        imageRenderer.requestZoom(
            currentZoom + (currentZoom < 0.5 ? 0.05 : 0.1)
        )
    }

    function zoomImageOut() {
        var currentZoom = imageRenderer.effectiveScale
        imageRenderer.requestZoom(
            currentZoom - (currentZoom <= 0.5 ? 0.05 : 0.1)
        )
    }

    function fitImage() {
        root.imageFitToView = true
        root.imageZoom = 1.0
        root.updateViewportState({
            imageFitToView: true,
            imageZoom: 1.0,
            imageFocusX: 0.5,
            imageFocusY: 0.5
        })
        Qt.callLater(function() {
            imageRenderer.restoreViewport(0, 0, 0.5, 0.5)
        })
    }

    function showImageActualSize() {
        root.imageFitToView = false
        root.imageZoom = 1.0
        root.updateViewportState({
            imageFitToView: false,
            imageZoom: 1.0,
            imageFocusX: 0.5,
            imageFocusY: 0.5
        })
        Qt.callLater(function() {
            imageRenderer.restoreViewport(0, 0, 0.5, 0.5)
        })
    }

    function zoomActiveRendererIn() {
        if (root.documentRenderingAvailable) {
            pdfRenderer.requestZoom(
                Math.min(6.0, root.documentZoom + 0.1)
            )
            return
        }

        if (root.imageRenderingAvailable) {
            root.zoomImageIn()
            return
        }

        root.zoomMarkdownIn()
    }

    function zoomActiveRendererOut() {
        if (root.documentRenderingAvailable) {
            pdfRenderer.requestZoom(
                Math.max(0.2, root.documentZoom - 0.1)
            )
            return
        }

        if (root.imageRenderingAvailable) {
            root.zoomImageOut()
            return
        }

        root.zoomMarkdownOut()
    }

    function resetActiveRendererZoom() {
        if (root.documentRenderingAvailable) {
            pdfRenderer.requestZoom(1.0)
            return
        }

        if (root.imageRenderingAvailable) {
            root.showImageActualSize()
            return
        }

        root.resetMarkdownZoom()
    }

    function prepareDocument() {
        if (!root.documentRenderingAvailable) {
            return
        }

        documentPreview.openDocument(
            String(LibraryStore.selectedLibrary.rootPath || ""),
            String(root.file && root.file.relativePath ? root.file.relativePath : "")
        )
    }

    onViewportStateKeyChanged: Qt.callLater(function() {
        root.restoreViewportState(root.viewportStateKey)
        root.prepareDocument()
    })

    onViewModeChanged: {
        if (!root.restoringViewportState) {
            root.updateViewportState({
                viewMode: root.viewMode
            })
            Qt.callLater(root.restoreVisibleViewport)
        }
    }

    onImageFitToViewChanged: {
        root.updateViewportState({
            imageFitToView: root.imageFitToView
        })
    }

    onImageCheckerboardVisibleChanged: {
        root.updateViewportState({
            imageCheckerboardVisible: root.imageCheckerboardVisible
        })
    }

    onDocumentFitToWidthChanged: {
        root.updateViewportState({
            documentFitToWidth: root.documentFitToWidth
        })
    }

    DocumentPreviewService {
        id: documentPreview
    }

    Connections {
        target: LibraryStore

        function onSelectedLibraryChanged() {
            if (root.documentRenderingAvailable) {
                Qt.callLater(root.prepareDocument)
            }
        }
    }

    Component.onCompleted: {
        restoreViewportState(root.viewportStateKey)
        prepareDocument()
    }

    Component.onDestruction: {
        viewportStateSaveTimer.stop()
        saveViewportState(root.activeViewportStateKey)
        WorkspaceState.sync()
        documentPreview.clear()
    }

    Timer {
        id: viewportStateSaveTimer

        interval: 180
        repeat: false
        onTriggered: root.saveViewportState(root.activeViewportStateKey)
    }

    Timer {
        id: viewportStateRestoreTimer

        interval: 36
        repeat: false
        onTriggered: root.applyPendingViewportState()
    }

    Shortcut {
        sequence: StandardKey.ZoomIn
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererIn()
    }

    Shortcut {
        sequence: "Ctrl+="
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererIn()
    }

    Shortcut {
        sequence: StandardKey.ZoomOut
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererOut()
    }

    Shortcut {
        sequence: "Ctrl+0"
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
                    visible: root.imageRenderingAvailable || root.documentRenderingAvailable
                    text: "Fit"
                    hoverEnabled: true
                    padding: 0
                    onClicked: {
                        if (root.documentRenderingAvailable) {
                            if (root.documentFitToWidth) {
                                pdfRenderer.fitWidth()
                            } else {
                                root.documentFitToWidth = true
                            }
                        } else {
                            root.fitImage()
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: root.documentRenderingAvailable
                        ? "Fit document to width"
                        : "Fit image to viewport"

                    contentItem: Text {
                        text: parent.text
                        color: (root.documentRenderingAvailable && root.documentFitToWidth)
                            || (root.imageRenderingAvailable && root.imageFitToView)
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: (root.documentRenderingAvailable && root.documentFitToWidth)
                            || (root.imageRenderingAvailable && root.imageFitToView)
                            ? root.theme.activeBg
                            : parent.hovered
                                ? root.theme.hoverBg
                                : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: (root.documentRenderingAvailable && root.documentFitToWidth)
                            || (root.imageRenderingAvailable && root.imageFitToView)
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
                    text: root.documentRenderingAvailable
                        ? String(pdfRenderer.effectivePercent) + "%"
                        : root.imageRenderingAvailable
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
                id: sourceRenderer

                anchors.fill: parent
                visible: !root.loading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                    && root.viewMode === "source"
                theme: root.theme
                content: root.content
                onViewportChanged: function(contentX, contentY, xRatio, yRatio) {
                    if (!visible) {
                        return
                    }

                    root.updateViewportState({
                        sourceX: contentX,
                        sourceY: contentY,
                        sourceXRatio: xRatio,
                        sourceYRatio: yRatio
                    })
                }
            }

            MarkdownRenderer {
                id: markdownRenderer

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
                onViewportChanged: function(contentX, contentY, xRatio, yRatio) {
                    if (!visible) {
                        return
                    }

                    root.updateViewportState({
                        markdownX: contentX,
                        markdownY: contentY,
                        markdownXRatio: xRatio,
                        markdownYRatio: yRatio
                    })
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
                onZoomFactorRequested: function(value, focusX, focusY) {
                    root.imageFitToView = false
                    root.setImageZoom(value, focusX, focusY)
                }
                onFitRequested: root.fitImage()
                onActualSizeRequested: root.showImageActualSize()
                onViewportChanged: function(contentX, contentY, focusX, focusY) {
                    if (!visible) {
                        return
                    }

                    root.updateViewportState({
                        imageViewportVersion: 1,
                        imageX: contentX,
                        imageY: contentY,
                        imageFocusX: focusX,
                        imageFocusY: focusY
                    })
                }
            }

            PdfRenderer {
                id: pdfRenderer

                anchors.fill: parent
                visible: root.documentRenderingAvailable
                    && documentPreview.state === "ready"
                theme: root.theme
                source: documentPreview.previewUrl
                zoomFactor: root.documentZoom
                fitToWidth: root.documentFitToWidth
                onZoomFactorRequested: function(value) {
                    root.documentFitToWidth = false
                    root.documentZoom = value
                    root.updateViewportState({
                        documentFitToWidth: false,
                        documentZoom: value
                    })
                }
                onViewportChanged: function(
                    page,
                    zoomFactor,
                    fitToWidth,
                    contentX,
                    contentY,
                    focusX,
                    focusY
                ) {
                    if (!visible) {
                        return
                    }

                    root.documentZoom = zoomFactor
                    root.documentFitToWidth = fitToWidth
                    root.updateViewportState({
                        documentPage: page,
                        documentViewportVersion: 5,
                        documentZoom: zoomFactor,
                        documentFitToWidth: fitToWidth,
                        documentX: contentX,
                        documentY: contentY,
                        documentFocusX: focusX,
                        documentFocusY: focusY
                    })
                }
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
                    id: splitSourceRenderer

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 260
                    theme: root.theme
                    content: root.content
                    onViewportChanged: function(contentX, contentY, xRatio, yRatio) {
                        if (!visible) {
                            return
                        }

                        root.updateViewportState({
                            splitSourceX: contentX,
                            splitSourceY: contentY,
                            splitSourceXRatio: xRatio,
                            splitSourceYRatio: yRatio
                        })
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: root.theme.quietBorder
                }

                MarkdownRenderer {
                    id: splitMarkdownRenderer

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
                    onViewportChanged: function(contentX, contentY, xRatio, yRatio) {
                        if (!visible) {
                            return
                        }

                        root.updateViewportState({
                            splitMarkdownX: contentX,
                            splitMarkdownY: contentY,
                            splitMarkdownXRatio: xRatio,
                            splitMarkdownYRatio: yRatio
                        })
                    }
                }
            }

            DocumentLoadingState {
                anchors.centerIn: parent
                width: Math.min(440, parent.width - 48)
                visible: root.documentRenderingAvailable && root.displayLoading
                theme: root.theme
                title: documentPreview.converterLabel === "LibreOffice"
                    ? "Preparing Office preview"
                    : "Opening document"
                detail: documentPreview.converterLabel === "LibreOffice"
                    ? "Converting a private local preview. The original file stays untouched."
                    : "Resolving the document and preparing its pages."
                fileLabel: root.file && root.file.name
                    ? String(root.file.name)
                    : ""
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 60)
                spacing: 8
                visible: (!root.documentRenderingAvailable && root.displayLoading)
                    || root.displayErrorMessage.length > 0
                    || (!root.displayContentAvailable && !root.documentRenderingAvailable)

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
