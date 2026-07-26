import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Pdf

Item {
    id: root

    required property var theme
    property url source
    property real zoomFactor: 1.0
    property bool fitToWidth: false

    property real singleRenderScale: 1.0
    property real singleCanvasWidth: 1
    property real singleCanvasHeight: 1
    property real lastViewportWidth: 1
    property real lastViewportHeight: 1
    property bool singleInitialized: false
    property bool adjustingSingleViewport: false
    property bool requestingZoom: false

    property int pendingMultiPage: 0
    property real pendingMultiZoom: 1.0
    property bool pendingMultiFit: false

    property bool multiZoomPreviewActive: false
    property bool multiZoomCommitPending: false
    property real multiZoomBaseScale: 1.0
    property real multiZoomPreviewScale: 1.0
    property real multiZoomAnchorX: 0
    property real multiZoomAnchorY: 0
    property real multiZoomPreviewOpacity: 0
    property int multiZoomRevision: 0
    property int multiReadyPasses: 0
    property int multiReadyChecks: 0
    property real pinchStartZoom: 1.0
    property real pinchAnchorX: 0
    property real pinchAnchorY: 0

    readonly property bool documentReady:
        document.status === PdfDocument.Ready
    readonly property bool singlePageDocument:
        root.documentReady && document.pageCount === 1
    readonly property size singlePagePointSize:
        root.singlePageDocument
            ? document.pagePointSize(0)
            : Qt.size(1, 1)
    readonly property real singlePageWidth: Math.max(
        1,
        root.singlePagePointSize.width
            * root.singleRenderScale
    )
    readonly property real singlePageHeight: Math.max(
        1,
        root.singlePagePointSize.height
            * root.singleRenderScale
    )
    readonly property real singlePanMarginX: Math.max(
        1,
        singleViewport.width
    )
    readonly property real singlePanMarginY: Math.max(
        1,
        singleViewport.height
    )
    readonly property real singleMaximumX: Math.max(
        0,
        root.singleCanvasWidth - singleViewport.width
    )
    readonly property real singleMaximumY: Math.max(
        0,
        root.singleCanvasHeight - singleViewport.height
    )
    readonly property real activeRenderScale:
        root.singlePageDocument
            ? root.singleRenderScale
            : multiPageView.renderScale
    readonly property int effectivePercent: Math.round(
        root.activeRenderScale * 100
    )
    readonly property int currentPage:
        root.singlePageDocument
            ? 0
            : Math.max(0, multiPageView.currentPage)

    signal zoomFactorRequested(real value)
    signal viewportChanged(
        int page,
        real zoomFactor,
        bool fitToWidth,
        real contentX,
        real contentY,
        real focusX,
        real focusY
    )

    clip: true

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function clampedZoom(value) {
        return root.clamp(Number(value || 1.0), 0.2, 6.0)
    }

    function singleFitScale() {
        return root.clampedZoom(
            Math.max(1, singleViewport.width - 64)
                / Math.max(1, root.singlePagePointSize.width)
        )
    }

    function updateSingleCanvas(pageWidth, pageHeight) {
        root.singleCanvasWidth = Math.max(
            singleViewport.width,
            root.singlePanMarginX * 2 + pageWidth
        )
        root.singleCanvasHeight = Math.max(
            singleViewport.height,
            root.singlePanMarginY * 2 + pageHeight
        )
    }

    function pointInsideSinglePage(viewportX, viewportY) {
        if (!root.singlePageDocument) {
            return false
        }

        var pageLeft =
            singlePageFrame.x - singleViewport.contentX
        var pageTop =
            singlePageFrame.y - singleViewport.contentY

        return viewportX >= pageLeft
            && viewportX <= pageLeft + root.singlePageWidth
            && viewportY >= pageTop
            && viewportY <= pageTop + root.singlePageHeight
    }

    function singleFocusAt(viewportX, viewportY) {
        if (!root.singlePageDocument) {
            return Qt.point(0.5, 0.5)
        }

        return Qt.point(
            root.clamp(
                (
                    singleViewport.contentX
                    + viewportX
                    - singlePageFrame.x
                ) / Math.max(1, root.singlePageWidth),
                0,
                1
            ),
            root.clamp(
                (
                    singleViewport.contentY
                    + viewportY
                    - singlePageFrame.y
                ) / Math.max(1, root.singlePageHeight),
                0,
                1
            )
        )
    }

    function placeSingleFocus(
        focusX,
        focusY,
        anchorX,
        anchorY,
        pageWidth,
        pageHeight
    ) {
        var targetX = root.singlePanMarginX
            + root.clamp(Number(focusX), 0, 1) * pageWidth
            - anchorX
        var targetY = root.singlePanMarginY
            + root.clamp(Number(focusY), 0, 1) * pageHeight
            - anchorY

        singleViewport.contentX = root.clamp(
            targetX,
            0,
            root.singleMaximumX
        )
        singleViewport.contentY = root.clamp(
            targetY,
            0,
            root.singleMaximumY
        )
    }

    function centerSinglePage() {
        if (!root.singlePageDocument) {
            return
        }

        root.adjustingSingleViewport = true
        root.updateSingleCanvas(
            root.singlePageWidth,
            root.singlePageHeight
        )
        root.placeSingleFocus(
            0.5,
            0.5,
            singleViewport.width / 2,
            singleViewport.height / 2,
            root.singlePageWidth,
            root.singlePageHeight
        )
        root.adjustingSingleViewport = false
        root.singleInitialized = true
        viewportSaveTimer.restart()
    }

    function applySingleZoom(
        value,
        anchorX,
        anchorY,
        usePointerAnchor,
        notifyParent
    ) {
        if (!root.singlePageDocument) {
            return
        }

        var resolvedAnchorX = isFinite(Number(anchorX))
            ? root.clamp(
                Number(anchorX),
                0,
                singleViewport.width
            )
            : singleViewport.width / 2
        var resolvedAnchorY = isFinite(Number(anchorY))
            ? root.clamp(
                Number(anchorY),
                0,
                singleViewport.height
            )
            : singleViewport.height / 2
        var pointerIsUsable = Boolean(usePointerAnchor)
            && root.pointInsideSinglePage(
                resolvedAnchorX,
                resolvedAnchorY
            )

        if (!pointerIsUsable) {
            resolvedAnchorX = singleViewport.width / 2
            resolvedAnchorY = singleViewport.height / 2
        }

        var focus = root.singleFocusAt(
            resolvedAnchorX,
            resolvedAnchorY
        )
        var nextScale = root.clampedZoom(value)
        var nextWidth = Math.max(
            1,
            root.singlePagePointSize.width * nextScale
        )
        var nextHeight = Math.max(
            1,
            root.singlePagePointSize.height * nextScale
        )

        root.adjustingSingleViewport = true
        root.singleRenderScale = nextScale
        root.updateSingleCanvas(nextWidth, nextHeight)
        root.placeSingleFocus(
            focus.x,
            focus.y,
            resolvedAnchorX,
            resolvedAnchorY,
            nextWidth,
            nextHeight
        )
        root.adjustingSingleViewport = false
        root.singleInitialized = true

        if (notifyParent) {
            root.requestingZoom = true
            root.zoomFactorRequested(nextScale)
            root.requestingZoom = false
        }

        viewportSaveTimer.restart()
    }

    function beginMultiZoomPreview(anchorX, anchorY) {
        root.multiZoomBaseScale = Math.max(
            0.0001,
            multiPageView.renderScale
        )
        root.multiZoomAnchorX = isFinite(Number(anchorX))
            ? root.clamp(
                Number(anchorX),
                0,
                multiPageStage.width
            )
            : multiPageStage.width / 2
        root.multiZoomAnchorY = isFinite(Number(anchorY))
            ? root.clamp(
                Number(anchorY),
                0,
                multiPageStage.height
            )
            : multiPageStage.height / 2
        root.multiZoomPreviewScale = 1.0
        root.multiZoomPreviewOpacity = 1.0
        root.multiZoomPreviewActive = true
        root.multiZoomCommitPending = false
        multiPageSnapshot.scheduleUpdate()
    }

    function requestMultiZoom(value, anchorX, anchorY) {
        var nextZoom = root.clampedZoom(value)

        if (!root.multiZoomPreviewActive) {
            root.beginMultiZoomPreview(anchorX, anchorY)
        }

        root.multiZoomRevision += 1
        root.pendingMultiPage = root.currentPage
        root.pendingMultiZoom = nextZoom
        root.pendingMultiFit = false
        root.multiZoomPreviewScale =
            nextZoom / Math.max(
                0.0001,
                root.multiZoomBaseScale
            )

        root.requestingZoom = true
        root.zoomFactorRequested(nextZoom)
        root.requestingZoom = false

        multiZoomCommitTimer.restart()
    }

    function commitMultiZoom() {
        if (
            !root.documentReady
            || document.pageCount <= 1
            || !root.multiZoomPreviewActive
        ) {
            return
        }

        root.multiZoomCommitPending = true
        root.multiReadyPasses = 0
        root.multiReadyChecks = 0
        multiPageView.renderScale = root.pendingMultiZoom
        multiPageView.goToPage(root.pendingMultiPage)
        multiZoomReadyTimer.restart()
    }

    function checkMultiZoomReady() {
        if (
            !root.multiZoomPreviewActive
            || !root.multiZoomCommitPending
        ) {
            return
        }

        root.multiReadyChecks += 1

        if (
            multiPageView.currentPageRenderingStatus === Image.Ready
        ) {
            root.multiReadyPasses += 1
        } else {
            root.multiReadyPasses = 0
        }

        if (
            root.multiReadyPasses < 3
            && root.multiReadyChecks < 18
        ) {
            multiZoomReadyTimer.restart()
            return
        }

        root.multiZoomCommitPending = false
        root.multiZoomPreviewOpacity = 0
        multiZoomFinishTimer.restart()
        viewportSaveTimer.restart()
    }

    function finishMultiZoomPreview() {
        if (
            root.multiZoomCommitPending
            || multiZoomCommitTimer.running
        ) {
            return
        }

        root.multiZoomPreviewActive = false
        root.multiZoomPreviewScale = 1.0
        root.multiZoomPreviewOpacity = 0
    }

    function requestZoom(value, viewportX, viewportY) {
        if (root.singlePageDocument) {
            root.applySingleZoom(
                value,
                viewportX,
                viewportY,
                isFinite(Number(viewportX))
                    && isFinite(Number(viewportY)),
                true
            )
            return
        }

        root.requestMultiZoom(
            value,
            viewportX,
            viewportY
        )
    }

    function restoreViewport(
        page,
        zoom,
        fitToWidth,
        contentX,
        contentY,
        focusX,
        focusY
    ) {
        if (!root.documentReady) {
            root.pendingMultiPage = Math.max(0, Number(page || 0))
            root.pendingMultiZoom = root.clampedZoom(zoom)
            root.pendingMultiFit = Boolean(fitToWidth)
            return
        }

        if (root.singlePageDocument) {
            root.adjustingSingleViewport = true
            root.singleRenderScale = root.clampedZoom(zoom)
            root.updateSingleCanvas(
                root.singlePageWidth,
                root.singlePageHeight
            )
            root.placeSingleFocus(
                isFinite(Number(focusX)) ? Number(focusX) : 0.5,
                isFinite(Number(focusY)) ? Number(focusY) : 0.5,
                singleViewport.width / 2,
                singleViewport.height / 2,
                root.singlePageWidth,
                root.singlePageHeight
            )
            root.adjustingSingleViewport = false
            root.singleInitialized = true
            viewportSaveTimer.restart()
            return
        }

        root.pendingMultiPage = Math.max(0, Number(page || 0))
        root.pendingMultiZoom = root.clampedZoom(zoom)
        root.pendingMultiFit = Boolean(fitToWidth)
        root.applyPendingMultiPageState()
    }

    function applyPendingMultiPageState() {
        if (!root.documentReady || document.pageCount <= 1) {
            return
        }

        if (root.pendingMultiFit) {
            multiPageView.scaleToWidth(
                Math.max(1, multiPageStage.width),
                Math.max(1, multiPageStage.height)
            )
        } else {
            multiPageView.renderScale = root.pendingMultiZoom
        }

        multiPageView.goToPage(
            root.clamp(
                root.pendingMultiPage,
                0,
                Math.max(0, document.pageCount - 1)
            )
        )
        viewportSaveTimer.restart()
    }

    function fitWidth() {
        if (!root.documentReady) {
            return
        }

        if (root.singlePageDocument) {
            root.applySingleZoom(
                root.singleFitScale(),
                singleViewport.width / 2,
                singleViewport.height / 2,
                false,
                true
            )
            return
        }

        root.multiZoomRevision += 1
        root.multiZoomCommitPending = false
        root.multiZoomPreviewActive = false
        root.multiZoomPreviewOpacity = 0
        multiZoomCommitTimer.stop()
        multiZoomReadyTimer.stop()
        multiZoomFinishTimer.stop()
        root.pendingMultiPage = root.currentPage
        root.pendingMultiFit = true
        root.applyPendingMultiPageState()
    }

    function preserveSingleAcrossResize() {
        if (
            !root.singlePageDocument
            || !root.singleInitialized
        ) {
            root.lastViewportWidth = Math.max(
                1,
                singleViewport.width
            )
            root.lastViewportHeight = Math.max(
                1,
                singleViewport.height
            )
            return
        }

        var oldWidth = Math.max(1, root.lastViewportWidth)
        var oldHeight = Math.max(1, root.lastViewportHeight)
        var focusX = root.clamp(
            (
                singleViewport.contentX
                + oldWidth / 2
                - oldWidth
            ) / Math.max(1, root.singlePageWidth),
            0,
            1
        )
        var focusY = root.clamp(
            (
                singleViewport.contentY
                + oldHeight / 2
                - oldHeight
            ) / Math.max(1, root.singlePageHeight),
            0,
            1
        )

        root.lastViewportWidth = Math.max(
            1,
            singleViewport.width
        )
        root.lastViewportHeight = Math.max(
            1,
            singleViewport.height
        )
        root.adjustingSingleViewport = true
        root.updateSingleCanvas(
            root.singlePageWidth,
            root.singlePageHeight
        )
        root.placeSingleFocus(
            focusX,
            focusY,
            singleViewport.width / 2,
            singleViewport.height / 2,
            root.singlePageWidth,
            root.singlePageHeight
        )
        root.adjustingSingleViewport = false
        viewportSaveTimer.restart()
    }

    function emitViewportChanged() {
        if (!root.documentReady) {
            return
        }

        var focus = root.singlePageDocument
            ? root.singleFocusAt(
                singleViewport.width / 2,
                singleViewport.height / 2
            )
            : Qt.point(0.5, 0.5)

        root.viewportChanged(
            root.currentPage,
            root.activeRenderScale,
            root.fitToWidth,
            root.singlePageDocument
                ? singleViewport.contentX
                : 0,
            root.singlePageDocument
                ? singleViewport.contentY
                : 0,
            focus.x,
            focus.y
        )
    }

    onZoomFactorChanged: {
        if (
            root.documentReady
            && !root.fitToWidth
            && !root.requestingZoom
            && Math.abs(
                root.activeRenderScale - root.zoomFactor
            ) > 0.0001
        ) {
            root.requestZoom(root.zoomFactor)
        }
    }

    onFitToWidthChanged: {
        if (root.documentReady && root.fitToWidth) {
            root.fitWidth()
        }
    }

    onSourceChanged: {
        root.singleInitialized = false
        root.pendingMultiPage = 0
    }

    Component.onCompleted: {
        root.lastViewportWidth = Math.max(
            1,
            singleViewport.width
        )
        root.lastViewportHeight = Math.max(
            1,
            singleViewport.height
        )
        root.singleRenderScale = root.clampedZoom(
            root.zoomFactor
        )
    }

    PdfDocument {
        id: document
        source: root.source

        onStatusChanged: {
            if (status !== PdfDocument.Ready) {
                return
            }

            if (document.pageCount === 1) {
                root.singleRenderScale = root.clampedZoom(
                    root.zoomFactor
                )
                root.centerSinglePage()
            } else {
                root.applyPendingMultiPageState()
            }
        }
    }

    Flickable {
        id: singleViewport

        anchors.fill: parent
        visible: root.singlePageDocument
        clip: true
        acceptedButtons: Qt.NoButton
        contentWidth: root.singleCanvasWidth
        contentHeight: root.singleCanvasHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        onWidthChanged: root.preserveSingleAcrossResize()
        onHeightChanged: root.preserveSingleAcrossResize()

        onContentXChanged: {
            if (!root.adjustingSingleViewport) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (!root.adjustingSingleViewport) {
                viewportSaveTimer.restart()
            }
        }

        onMovementEnded: root.emitViewportChanged()

        Item {
            id: singleCanvas

            width: singleViewport.contentWidth
            height: singleViewport.contentHeight

            Rectangle {
                x: singlePageFrame.x + 8
                y: singlePageFrame.y + 10
                width: singlePageFrame.width
                height: singlePageFrame.height
                visible: singlePageImage.status === Image.Ready
                color: "#6f000000"
                radius: 2
            }

            Rectangle {
                id: singlePageFrame

                x: root.singlePanMarginX
                y: root.singlePanMarginY
                width: root.singlePageWidth
                height: root.singlePageHeight
                color: "white"
                border.width: 1
                border.color: root.theme.panelBorder
                radius: 1
                clip: true

                PdfPageImage {
                    id: singlePageImage

                    anchors.fill: parent
                    document: document
                    currentFrame: 0
                    sourceSize: Qt.size(
                        Math.max(
                            1,
                            Math.ceil(root.singlePageWidth)
                        ),
                        Math.max(
                            1,
                            Math.ceil(root.singlePageHeight)
                        )
                    )
                    asynchronous: true
                    retainWhileLoading: true
                    cache: false
                    smooth: true
                    fillMode: Image.Stretch
                }
            }
        }
    }

    WheelHandler {
        enabled: root.documentReady
        acceptedDevices:
            PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier
        blocking: true

        onWheel: function(event) {
            var direction =
                event.angleDelta.y >= 0 ? 1 : -1
            var step = root.activeRenderScale < 0.5
                ? 0.05
                : 0.1

            root.requestZoom(
                root.activeRenderScale + direction * step,
                point.position.x,
                point.position.y
            )
            event.accepted = true
        }
    }

    PinchHandler {
        id: documentPinchHandler

        enabled: root.documentReady
        target: null
        acceptedDevices:
            PointerDevice.TouchPad | PointerDevice.TouchScreen
        scaleAxis.enabled: true

        onActiveChanged: {
            if (active) {
                root.pinchStartZoom =
                    root.activeRenderScale
                root.pinchAnchorX =
                    centroid.position.x
                root.pinchAnchorY =
                    centroid.position.y
            }
        }

        onActiveScaleChanged: {
            if (active) {
                root.requestZoom(
                    root.pinchStartZoom * activeScale,
                    root.pinchAnchorX,
                    root.pinchAnchorY
                )
            }
        }
    }

    Item {
        id: multiPageStage

        anchors.fill: parent
        anchors.topMargin: 28
        anchors.bottomMargin: 28
        visible: root.documentReady
            && document.pageCount > 1
        clip: true

        PdfMultiPageView {
            id: multiPageView

            anchors.fill: parent
            clip: true
            document: document

            onCurrentPageChanged: {
                viewportSaveTimer.restart()
            }

            onCurrentPageRenderingStatusChanged: {
                if (root.multiZoomCommitPending) {
                    multiZoomReadyTimer.restart()
                }
            }

            onRenderScaleChanged: {
                if (
                    root.documentReady
                    && !root.requestingZoom
                    && !root.multiZoomCommitPending
                    && Math.abs(
                        root.zoomFactor - renderScale
                    ) > 0.0001
                ) {
                    root.requestingZoom = true
                    root.zoomFactorRequested(renderScale)
                    root.requestingZoom = false
                    viewportSaveTimer.restart()
                }
            }
        }

        ShaderEffectSource {
            id: multiPageSnapshot

            anchors.fill: parent
            sourceItem: multiPageView
            sourceRect: Qt.rect(
                0,
                0,
                multiPageView.width,
                multiPageView.height
            )
            live: false
            hideSource: false
            smooth: true
            mipmap: true
            visible: root.multiZoomPreviewActive
            opacity: root.multiZoomPreviewOpacity
            z: 20

            transform: Scale {
                origin.x: root.multiZoomAnchorX
                origin.y: root.multiZoomAnchorY
                xScale: root.multiZoomPreviewScale
                yScale: root.multiZoomPreviewScale
            }

            layer.enabled: root.multiZoomPreviewActive
            layer.effect: MultiEffect {
                autoPaddingEnabled: false
                blurEnabled: true
                blurMax: 4
                blur: root.multiZoomPreviewActive ? 0.12 : 0
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 90
                    easing.type: Easing.OutCubic
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: root.multiZoomPreviewActive
            color: "#10000000"
            z: 21
        }
    }

    Timer {
        id: multiZoomCommitTimer

        interval: 110
        repeat: false
        onTriggered: root.commitMultiZoom()
    }

    Timer {
        id: multiZoomReadyTimer

        interval: 42
        repeat: false
        onTriggered: root.checkMultiZoomReady()
    }

    Timer {
        id: multiZoomFinishTimer

        interval: 110
        repeat: false
        onTriggered: root.finishMultiZoomPreview()
    }

    Timer {
        id: viewportSaveTimer

        interval: 90
        repeat: false
        onTriggered: root.emitViewportChanged()
    }

    DocumentLoadingState {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 48)
        visible: document.status === PdfDocument.Loading
            || (
                root.singlePageDocument
                && singlePageImage.status === Image.Loading
            )
        theme: root.theme
        title: "Laying out pages"
        detail: "Archivist is rendering a local document surface."
    }

    Rectangle {
        anchors.fill: parent
        visible: document.status === PdfDocument.Error
            || (
                root.singlePageDocument
                && singlePageImage.status === Image.Error
            )
        color: root.theme.workspaceBgDeep

        Text {
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 60)
            text: document.status === PdfDocument.Error
                && document.error.length > 0
                    ? document.error
                    : "Qt could not render this PDF."
            color: root.theme.danger
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: root.theme.typeSize(11)
        }
    }
}
