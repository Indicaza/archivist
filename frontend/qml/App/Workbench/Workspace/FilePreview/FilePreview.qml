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
    property string trackedInitialFileId: ""
    property bool initialFileLoaded: false

    readonly property string currentFileId:
        file && file.id ? String(file.id) : ""

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
    readonly property bool markdownRenderingAvailable: Boolean(
        rendererSelection
        && rendererSelection.id === "markdown"
        && rendererSelection.available
    )
    readonly property bool imageRenderingAvailable: Boolean(
        rendererSelection
        && rendererSelection.id === "image"
        && rendererSelection.available
    )
    readonly property bool documentRenderingAvailable: Boolean(
        rendererSelection
        && rendererSelection.id === "pdf"
        && rendererSelection.available
    )
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
    readonly property bool previewMatchesFile: Boolean(
        preview
        && preview.file
        && String(preview.file.id || "") === currentFileId
    )
    readonly property bool blockingInitialLoading:
        displayLoading && !initialFileLoaded
    readonly property bool blockingInitialError:
        displayErrorMessage.length > 0 && !initialFileLoaded
    property string viewMode: "rendered"
    property real markdownZoom: 1.0
    property real imageZoom: 1.0
    property bool imageFitToView: false
    property bool imageCheckerboardVisible: true
    property real documentZoom: 1.0
    property bool documentFitToWidth: false
    property bool documentFreePanEnabled: false
    property string activeViewportStateKey: ""
    property var cachedViewportState: ({})
    property bool restoringViewportState: false
    property int viewportRestorePass: 0

    function updateInitialFileState() {
        if (trackedInitialFileId !== currentFileId) {
            trackedInitialFileId = currentFileId
            initialFileLoaded = false
        }

        if (
            currentFileId.length > 0
            && (
                previewMatchesFile
                || displayContentAvailable
            )
        ) {
            initialFileLoaded = true
        }
    }

    onCurrentFileIdChanged: updateInitialFileState()
    onPreviewMatchesFileChanged: updateInitialFileState()
    onDisplayContentAvailableChanged: updateInitialFileState()

    component PreviewControlButton: Button {
        id: controlButton

        required property var controlTheme
        property bool activeControl: false
        property string controlTooltip: ""
        property bool compactLabel: false
        property bool circular: false

        width: 40
        height: circular
            ? 40
            : compactLabel
                ? 24
                : 32
        hoverEnabled: true
        padding: 0
        opacity: enabled ? 1.0 : 0.42
        scale: down
            ? controlTheme.pressedScale
            : hovered
                ? 1.06
                : 1.0

        ToolTip.visible: hovered
        ToolTip.delay: 220
        ToolTip.text: controlTooltip

        Behavior on scale {
            NumberAnimation {
                duration: controlTheme.motionHover
                easing.type: Easing.OutCubic
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: controlTheme.motionFast
            }
        }

        contentItem: Text {
            text: parent.text
            color: parent.activeControl
                ? controlTheme.accentBright
                : parent.hovered
                    ? controlTheme.appText
                    : controlTheme.mutedText
            font.pixelSize: controlTheme.typeSize(
                parent.compactLabel
                    ? 8
                    : String(parent.text).length > 3
                        ? 8
                        : 11
            )
            font.weight: parent.activeControl
                ? Font.Bold
                : Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: controlButton.circular
                ? width / 2
                : controlButton.compactLabel
                    ? 6
                    : 8
            color: controlButton.activeControl
                ? controlTheme.activeBg
                : controlButton.hovered
                    ? controlTheme.hoverBg
                    : controlButton.circular
                        ? Qt.rgba(
                            controlTheme.controlSurfaceBg.r,
                            controlTheme.controlSurfaceBg.g,
                            controlTheme.controlSurfaceBg.b,
                            0.88
                        )
                        : "transparent"
            border.width: controlButton.activeControl
                ? 2
                : controlButton.circular
                    || controlButton.hovered
                        ? 1
                        : 0
            border.color: controlButton.activeControl
                ? controlTheme.accent
                : controlButton.hovered
                    ? controlTheme.quietBorder
                    : controlTheme.panelBorder

            Behavior on color {
                ColorAnimation {
                    duration: controlTheme.motionFast
                }
            }

            Behavior on border.color {
                ColorAnimation {
                    duration: controlTheme.motionFast
                }
            }
        }
    }

    readonly property string viewportStateKey: (
        String(CollectionStore.selectedCollectionId || "").length > 0
        && String(LibraryStore.activeFileLibraryId || "").length > 0
        && String(file && file.id ? file.id : "").length > 0
    )
        ? "workspace/collections/"
            + String(CollectionStore.selectedCollectionId)
            + "/viewports/files/"
            + String(LibraryStore.activeFileLibraryId)
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
            documentFreePanEnabled: false,
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

        root.cachedViewportState = state
        var restoredViewMode = String(
            state.viewMode || root.defaultViewportState().viewMode
        )

        if (
            root.markdownRenderingAvailable
            && restoredViewMode !== "source"
            && restoredViewMode !== "rendered"
        ) {
            restoredViewMode = "rendered"
        }

        root.viewMode = restoredViewMode
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
        root.documentFreePanEnabled = Boolean(
            state.documentFreePanEnabled
        )

        viewportStateRestoreTimer.restart()
    }

    function restoreDocumentViewport() {
        if (!root.documentRenderingAvailable) {
            return
        }

        var state =
            root.cachedViewportState
            || root.defaultViewportState()
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
                Math.round(
                    root.numberValue(
                        state.documentPage,
                        0
                    )
                )
            ),
            root.numberValue(
                state.documentZoom,
                1.0
            ),
            Boolean(
                state.documentFitToWidth
            ),
            documentViewportVersion >= 5
                ? root.numberValue(
                    state.documentX,
                    0
                )
                : 0,
            documentViewportVersion >= 5
                ? root.numberValue(
                    state.documentY,
                    0
                )
                : 0,
            documentViewportVersion >= 5
                ? root.numberValue(
                    state.documentFocusX,
                    0.5
                )
                : 0.5,
            documentViewportVersion >= 5
                ? root.numberValue(
                    state.documentFocusY,
                    0.5
                )
                : 0.5
        )
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

        root.restoreDocumentViewport()
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
            String(LibraryStore.activeFileLibrary.rootPath || ""),
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

    onDocumentFreePanEnabledChanged: {
        root.updateViewportState({
            documentFreePanEnabled:
                root.documentFreePanEnabled
        })
    }

    DocumentPreviewService {
        id: documentPreview
    }

    Connections {
        target: documentPreview

        function onStateChanged() {
            if (
                documentPreview.state !== "ready"
                || !root.documentRenderingAvailable
            ) {
                return
            }

            Qt.callLater(
                root.restoreDocumentViewport
            )
        }
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
        updateInitialFileState()
        restoreViewportState(root.viewportStateKey)
        prepareDocument()
    }

    Component.onDestruction: {
        viewportStateSaveTimer.stop()
        viewportStateRestoreTimer.stop()
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
        sequences: [ StandardKey.ZoomIn ]
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererIn()
    }

    Shortcut {
        sequence: "Ctrl+="
        enabled: root.directRenderingAvailable && root.viewMode !== "source"
        onActivated: root.zoomActiveRendererIn()
    }

    Shortcut {
        sequences: [ StandardKey.ZoomOut ]
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
            && !ChatStore.responding
            && !ChatStore.mutating
            && !ChatStore.mutatingAttachment
    )
    readonly property string statusAccessLabel: "READ-ONLY"
    readonly property string statusTypeLabel: String(
        root.fileIdentity.displayLabel || "File"
    )
    readonly property string statusRendererLabel: String(
        root.rendererSelection.displayLabel || ""
    )
    readonly property string statusPath: root.file && root.file.relativePath
        ? String(root.file.relativePath)
        : "Library file"
    readonly property string statusMetrics: root.lineCount > 0
        ? root.formattedSize(root.sizeBytes)
            + "  ·  "
            + String(root.lineCount)
            + (root.lineCount === 1 ? " line" : " lines")
        : root.formattedSize(root.sizeBytes)

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
        anchors.leftMargin: Math.max(8, root.leftObstruction + 8)
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.theme.workspaceBgDeep
            border.width: 1
            border.color: root.theme.quietBorder
            radius: 5
            clip: true

            Item {
                id: floatingRendererControls

                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 10
                anchors.rightMargin: 10
                width: Math.max(
                    zoomControlGroup.visible
                        ? zoomControlGroup.width
                        : 0,
                    toolControlGroup.visible
                        ? toolControlGroup.width
                        : 0
                )
                height: zoomControlGroup.visible
                    ? zoomControlGroup.height
                        + (
                            toolControlGroup.visible
                                ? 10 + toolControlGroup.height
                                : 0
                        )
                    : toolControlGroup.height
                visible: root.directRenderingAvailable
                z: 180

                Rectangle {
                    id: zoomControlShadow

                    x: zoomControlGroup.x + 2
                    y: zoomControlGroup.y + 3
                    width: zoomControlGroup.width
                    height: zoomControlGroup.height
                    visible: zoomControlGroup.visible
                    radius: zoomControlGroup.radius
                    color: "#000000"
                    opacity: 0.22
                }

                Rectangle {
                    id: zoomControlGroup

                    anchors.top: parent.top
                    anchors.right: parent.right
                    width: 40
                    height: zoomControlColumn.implicitHeight + 10
                    visible: root.viewMode !== "source"
                    radius: 11
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: zoomControlsHover.hovered
                        ? root.theme.quietBorder
                        : root.theme.panelBorder
                    opacity: zoomControlsHover.hovered ? 0.99 : 0.88

                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.theme.motionFast
                        }
                    }

                    HoverHandler {
                        id: zoomControlsHover
                    }

                    Column {
                        id: zoomControlColumn

                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: 5
                        spacing: 3

                        PreviewControlButton {
                            controlTheme: root.theme
                            text: "+"
                            controlTooltip: "Zoom in"
                            onClicked:
                                root.zoomActiveRendererIn()
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            compactLabel: true
                            text:
                                root.documentRenderingAvailable
                                    ? String(
                                        pdfRenderer.effectivePercent
                                    ) + "%"
                                    : root.imageRenderingAvailable
                                        ? String(
                                            imageRenderer.effectivePercent
                                        ) + "%"
                                        : String(
                                            Math.round(
                                                root.markdownZoom * 100
                                            )
                                        ) + "%"
                            controlTooltip:
                                "Reset zoom"
                            onClicked:
                                root.resetActiveRendererZoom()
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            text: "−"
                            controlTooltip: "Zoom out"
                            onClicked:
                                root.zoomActiveRendererOut()
                        }
                    }
                }

                Rectangle {
                    id: toolControlGroup

                    anchors.top:
                        zoomControlGroup.visible
                            ? zoomControlGroup.bottom
                            : parent.top
                    anchors.topMargin:
                        zoomControlGroup.visible ? 10 : 0
                    anchors.right: parent.right
                    width: 40
                    height: toolControlColumn.implicitHeight
                    visible: root.directRenderingAvailable
                    color: "transparent"
                    border.width: 0

                    Column {
                        id: toolControlColumn

                        anchors.top: parent.top
                        anchors.horizontalCenter:
                            parent.horizontalCenter
                        spacing: 7

                        Repeater {
                            model:
                                root.markdownRenderingAvailable
                                && root.viewMode === "rendered"
                                    ? [
                                        {
                                            id: "source",
                                            label: "<>",
                                            tooltip:
                                                "Edit Markdown"
                                        }
                                    ]
                                    : []

                            PreviewControlButton {
                                required property var modelData

                                controlTheme: root.theme
                                circular: true
                                text: modelData.label
                                activeControl:
                                    root.viewMode
                                        === modelData.id
                                controlTooltip:
                                    modelData.tooltip
                                onClicked:
                                    root.viewMode =
                                        modelData.id
                            }
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible:
                                root.imageRenderingAvailable
                                || root.documentRenderingAvailable
                            text: "↔"
                            activeControl:
                                (
                                    root.documentRenderingAvailable
                                    && root.documentFitToWidth
                                )
                                || (
                                    root.imageRenderingAvailable
                                    && root.imageFitToView
                                )
                            controlTooltip:
                                root.documentRenderingAvailable
                                    ? "Fit document to width"
                                    : "Fit image to viewport"
                            onClicked: {
                                if (
                                    root.documentRenderingAvailable
                                ) {
                                    if (
                                        root.documentFitToWidth
                                    ) {
                                        pdfRenderer.fitWidth()
                                    } else {
                                        root.documentFitToWidth =
                                            true
                                    }
                                } else {
                                    root.fitImage()
                                }
                            }
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible:
                                root.documentRenderingAvailable
                            text: "◎"
                            controlTooltip:
                                "Center document"
                            onClicked:
                                pdfRenderer.recenter()
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible:
                                root.documentRenderingAvailable
                            text: "✣"
                            activeControl:
                                root.documentFreePanEnabled
                            controlTooltip:
                                root.documentFreePanEnabled
                                    ? "Return to reading mode"
                                    : "Enable free panning"
                            onClicked:
                                root.documentFreePanEnabled =
                                    !root.documentFreePanEnabled
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible:
                                root.imageRenderingAvailable
                            compactLabel: true
                            text: "1:1"
                            activeControl:
                                !root.imageFitToView
                                && root.imageZoom === 1.0
                            controlTooltip:
                                "Show actual pixel size"
                            onClicked:
                                root.showImageActualSize()
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible:
                                root.imageRenderingAvailable
                            text: "▦"
                            activeControl:
                                root.imageCheckerboardVisible
                            controlTooltip:
                                root.imageCheckerboardVisible
                                    ? "Hide transparency grid"
                                    : "Show transparency grid"
                            onClicked:
                                root.imageCheckerboardVisible =
                                    !root.imageCheckerboardVisible
                        }

                        PreviewControlButton {
                            controlTheme: root.theme
                            circular: true
                            visible: Boolean(
                                root.file
                                && root.file.id
                            )
                            enabled: root.canAttach
                            text:
                                root.pendingAttachmentFileId.length > 0
                                    ? "…"
                                    : root.attachedToChat
                                        ? "✓"
                                        : "+"
                            activeControl:
                                root.attachedToChat
                            controlTooltip:
                                ChatStore.selectedChatId.length === 0
                                    ? "Select a Chat before attaching this file"
                                    : root.attachedToChat
                                        ? "Remove this file from the selected Chat"
                                        : "Attach this file to the selected Chat"
                            onClicked: {
                                if (root.attachedToChat) {
                                    ChatStore.removeAttachment(
                                        root.attachmentId
                                    )
                                } else {
                                    root.pendingAttachmentFileId =
                                        String(root.file.id)
                                    ChatStore.attachFile(
                                        LibraryStore.activeFileLibraryId,
                                        root.pendingAttachmentFileId
                                    )
                                }
                            }
                        }
                    }
                }
            }

            PlainTextRenderer {
                id: sourceRenderer

                anchors.fill: parent
                visible: !root.blockingInitialLoading
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
                visible: !root.blockingInitialLoading
                    && root.errorMessage.length === 0
                    && root.content.length > 0
                    && root.markdownRenderingAvailable
                    && root.viewMode === "rendered"
                theme: root.theme
                content: root.content
                libraryRootPath: String(LibraryStore.activeFileLibrary.rootPath || "")
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
                libraryRootPath: String(LibraryStore.activeFileLibrary.rootPath || "")
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
                freePanEnabled:
                    root.documentFreePanEnabled
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
                visible: !root.blockingInitialLoading
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
                    libraryRootPath: String(LibraryStore.activeFileLibrary.rootPath || "")
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
                visible:
                    root.documentRenderingAvailable
                    && root.blockingInitialLoading
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
                visible:
                    (
                        !root.documentRenderingAvailable
                        && root.blockingInitialLoading
                    )
                    || root.blockingInitialError
                    || (
                        !root.displayContentAvailable
                        && !root.documentRenderingAvailable
                        && !root.initialFileLoaded
                    )

                Text {
                    width: parent.width
                    text: root.blockingInitialLoading
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
