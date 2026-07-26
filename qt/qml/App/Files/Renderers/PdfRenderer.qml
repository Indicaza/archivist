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
    property bool freePanEnabled: false

    property bool requestingZoom: false
    property bool documentSurfaceHasRendered: false

    property real singleRenderScale: 1.0
    property real singleCanvasWidth: 1
    property real singleCanvasHeight: 1
    property real lastSingleViewportWidth: 1
    property real lastSingleViewportHeight: 1
    property bool singleInitialized: false
    property bool adjustingSingleViewport: false
    property bool singlePageHasRendered: false
    property bool singlePageRefining: false
    property bool singleCenterAnimating: false
    property bool singleCenterChaseY: false
    property real singleCenterTargetX: 0
    property real singleCenterTargetY: 0
    property real singleCenterVelocityX: 0
    property real singleCenterVelocityY: 0

    property real multiRenderScale: 1.0
    property real multiCanvasWidth: 1
    property real multiCanvasHeight: 1
    property real multiDocumentWidth: 1
    property real multiDocumentHeight: 1
    property var multiPageWidths: []
    property var multiPageHeights: []
    property var multiPageTops: []
    property int multiCurrentPage: 0
    property real lastMultiViewportWidth: 1
    property real lastMultiViewportHeight: 1
    property bool multiInitialized: false
    property bool adjustingMultiViewport: false
    property bool multiVisualTransitionActive: false
    property bool multiCenterAnimating: false
    property real multiCenterTargetX: 0
    property real multiCenterVelocityX: 0

    property int pendingPage: 0
    property real pendingZoom: 1.0
    property bool pendingFitToWidth: false
    property real pendingFocusX: 0.5
    property real pendingFocusY: 0.5

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
        root.singleCanvasWidth
            - singleViewport.width
    )
    readonly property real singleMaximumY: Math.max(
        0,
        root.singleCanvasHeight
            - singleViewport.height
    )

    readonly property real multiPageGap: 28
    readonly property real readingEdgeInset: 32
    readonly property int centerFrameInterval: 16
    readonly property int centerDelayInterval: 28
    readonly property int resizeSettleInterval: 96
    readonly property int singleRefineInterval: 70
    readonly property int multiTransitionInterval: 115
    readonly property int viewportSaveInterval: 90
    readonly property real centerSpringStrength: 0.08
    readonly property real centerDamping: 0.68
    readonly property real centerPositionEpsilon: 0.18
    readonly property real centerVelocityEpsilon: 0.08
    readonly property real multiPanMarginX: Math.max(
        1,
        multiViewport.width
    )
    readonly property real multiPanMarginY: Math.max(
        1,
        multiViewport.height
    )
    readonly property real multiMaximumX: Math.max(
        0,
        root.multiCanvasWidth
            - multiViewport.width
    )
    readonly property real multiMaximumY: Math.max(
        0,
        root.multiCanvasHeight
            - multiViewport.height
    )

    readonly property real activeRenderScale:
        root.singlePageDocument
            ? root.singleRenderScale
            : root.multiRenderScale
    readonly property int effectivePercent: Math.round(
        root.activeRenderScale * 100
    )
    readonly property int currentPage:
        root.singlePageDocument
            ? 0
            : root.multiCurrentPage

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
        return Math.max(
            minimum,
            Math.min(maximum, value)
        )
    }

    function clampedZoom(value) {
        return root.clamp(
            Number(value || 1.0),
            0.2,
            6.0
        )
    }

    function singleFitScale() {
        return root.clampedZoom(
            Math.max(
                1,
                singleViewport.width - 64
            ) / Math.max(
                1,
                root.singlePagePointSize.width
            )
        )
    }

    function updateSingleCanvas(
        pageWidth,
        pageHeight
    ) {
        root.singleCanvasWidth = Math.max(
            singleViewport.width,
            root.singlePanMarginX * 2
                + pageWidth
        )
        root.singleCanvasHeight = Math.max(
            singleViewport.height,
            root.singlePanMarginY * 2
                + pageHeight
        )
    }

    function pointInsideSinglePage(
        viewportX,
        viewportY
    ) {
        if (!root.singlePageDocument) {
            return false
        }

        var pageLeft =
            singlePageFrame.x
            - singleViewport.contentX
        var pageTop =
            singlePageFrame.y
            - singleViewport.contentY

        return viewportX >= pageLeft
            && viewportX <= pageLeft
                + root.singlePageWidth
            && viewportY >= pageTop
            && viewportY <= pageTop
                + root.singlePageHeight
    }

    function singleFocusAt(
        viewportX,
        viewportY
    ) {
        if (!root.singlePageDocument) {
            return Qt.point(0.5, 0.5)
        }

        return Qt.point(
            root.clamp(
                (
                    singleViewport.contentX
                    + viewportX
                    - singlePageFrame.x
                ) / Math.max(
                    1,
                    root.singlePageWidth
                ),
                0,
                1
            ),
            root.clamp(
                (
                    singleViewport.contentY
                    + viewportY
                    - singlePageFrame.y
                ) / Math.max(
                    1,
                    root.singlePageHeight
                ),
                0,
                1
            )
        )
    }

    function singleFocusPosition(
        focusX,
        focusY,
        anchorX,
        anchorY,
        pageWidth,
        pageHeight
    ) {
        return {
            x: root.clamp(
                root.singlePanMarginX
                    + root.clamp(
                        Number(focusX),
                        0,
                        1
                    ) * pageWidth
                    - anchorX,
                0,
                root.singleMaximumX
            ),
            y: root.clamp(
                root.singlePanMarginY
                    + root.clamp(
                        Number(focusY),
                        0,
                        1
                    ) * pageHeight
                    - anchorY,
                0,
                root.singleMaximumY
            )
        }
    }

    function placeSingleFocus(
        focusX,
        focusY,
        anchorX,
        anchorY,
        pageWidth,
        pageHeight
    ) {
        var target = root.singleFocusPosition(
            focusX,
            focusY,
            anchorX,
            anchorY,
            pageWidth,
            pageHeight
        )

        singleViewport.contentX = target.x
        singleViewport.contentY = target.y
    }

    function applySingleZoom(
        value,
        anchorX,
        anchorY,
        usePointerAnchor,
        notifyParent
    ) {
        root.cancelSingleCenterAnimation()

        if (!root.singlePageDocument) {
            return
        }

        var resolvedAnchorX =
            isFinite(Number(anchorX))
                ? root.clamp(
                    Number(anchorX),
                    0,
                    singleViewport.width
                )
                : singleViewport.width / 2
        var resolvedAnchorY =
            isFinite(Number(anchorY))
                ? root.clamp(
                    Number(anchorY),
                    0,
                    singleViewport.height
                )
                : singleViewport.height / 2
        var pointerIsUsable =
            Boolean(usePointerAnchor)
            && root.pointInsideSinglePage(
                resolvedAnchorX,
                resolvedAnchorY
            )

        if (!pointerIsUsable) {
            resolvedAnchorX =
                singleViewport.width / 2
            resolvedAnchorY =
                singleViewport.height / 2
        }

        var focus = root.singleFocusAt(
            resolvedAnchorX,
            resolvedAnchorY
        )

        if (!root.freePanEnabled) {
            resolvedAnchorX =
                singleViewport.width / 2
            focus.x = 0.5
        }

        var nextScale =
            root.clampedZoom(value)
        var nextWidth = Math.max(
            1,
            root.singlePagePointSize.width
                * nextScale
        )
        var nextHeight = Math.max(
            1,
            root.singlePagePointSize.height
                * nextScale
        )

        root.adjustingSingleViewport = true
        root.singleRenderScale = nextScale
        root.updateSingleCanvas(
            nextWidth,
            nextHeight
        )
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
        root.constrainReadingPosition()

        if (notifyParent) {
            root.requestingZoom = true
            root.zoomFactorRequested(nextScale)
            root.requestingZoom = false
        }

        viewportSaveTimer.restart()
    }

    function preserveSingleAcrossResize() {
        if (
            !root.singlePageDocument
            || !root.singleInitialized
        ) {
            root.lastSingleViewportWidth =
                Math.max(
                    1,
                    singleViewport.width
                )
            root.lastSingleViewportHeight =
                Math.max(
                    1,
                    singleViewport.height
                )
            return
        }

        var oldWidth = Math.max(
            1,
            root.lastSingleViewportWidth
        )
        var oldHeight = Math.max(
            1,
            root.lastSingleViewportHeight
        )
        var focusX = root.clamp(
            (
                singleViewport.contentX
                + oldWidth / 2
                - oldWidth
            ) / Math.max(
                1,
                root.singlePageWidth
            ),
            0,
            1
        )
        var focusY = root.clamp(
            (
                singleViewport.contentY
                + oldHeight / 2
                - oldHeight
            ) / Math.max(
                1,
                root.singlePageHeight
            ),
            0,
            1
        )

        if (!root.freePanEnabled) {
            focusX = 0.5
        }

        root.lastSingleViewportWidth =
            Math.max(
                1,
                singleViewport.width
            )
        root.lastSingleViewportHeight =
            Math.max(
                1,
                singleViewport.height
            )

        if (root.fitToWidth) {
            root.singleRenderScale =
                root.singleFitScale()
        }

        root.updateSingleCanvas(
            root.singlePageWidth,
            root.singlePageHeight
        )

        var target = root.singleFocusPosition(
            focusX,
            focusY,
            singleViewport.width / 2,
            singleViewport.height / 2,
            root.singlePageWidth,
            root.singlePageHeight
        )

        if (root.freePanEnabled) {
            root.cancelSingleCenterAnimation()
            root.adjustingSingleViewport = true
            singleViewport.contentX = target.x
            singleViewport.contentY = target.y
            root.adjustingSingleViewport = false
            viewportSaveTimer.restart()
        } else {
            var bounds =
                root.readingVerticalBounds()
            root.animateSinglePosition(
                target.x,
                root.clamp(
                    singleViewport.contentY,
                    bounds.minimum,
                    bounds.maximum
                ),
                false
            )
            singleResizeSettleTimer.restart()
        }
    }

    function rebuildMultiLayout(scale) {
        if (
            !root.documentReady
            || document.pageCount <= 1
        ) {
            return
        }

        var nextScale =
            root.clampedZoom(scale)
        var widths = []
        var heights = []
        var tops = []
        var documentWidth = 1
        var documentHeight = 0

        for (
            var pageIndex = 0;
            pageIndex < document.pageCount;
            pageIndex += 1
        ) {
            var pageSize =
                document.pagePointSize(pageIndex)
            var pageWidth = Math.max(
                1,
                pageSize.width * nextScale
            )
            var pageHeight = Math.max(
                1,
                pageSize.height * nextScale
            )

            widths.push(pageWidth)
            heights.push(pageHeight)
            tops.push(documentHeight)

            documentWidth = Math.max(
                documentWidth,
                pageWidth
            )
            documentHeight += pageHeight

            if (
                pageIndex
                < document.pageCount - 1
            ) {
                documentHeight +=
                    root.multiPageGap
            }
        }

        root.multiRenderScale = nextScale
        root.multiPageWidths = widths
        root.multiPageHeights = heights
        root.multiPageTops = tops
        root.multiDocumentWidth =
            documentWidth
        root.multiDocumentHeight =
            Math.max(1, documentHeight)
        root.multiCanvasWidth = Math.max(
            multiViewport.width,
            root.multiPanMarginX * 2
                + documentWidth
        )
        root.multiCanvasHeight = Math.max(
            multiViewport.height,
            root.multiPanMarginY * 2
                + documentHeight
        )
    }

    function multiPageX(pageIndex) {
        var index = root.clamp(
            Math.round(Number(pageIndex || 0)),
            0,
            Math.max(
                0,
                root.multiPageWidths.length - 1
            )
        )
        var pageWidth =
            Number(
                root.multiPageWidths[index]
                || 1
            )

        return root.multiPanMarginX
            + (
                root.multiDocumentWidth
                - pageWidth
            ) / 2
    }

    function multiPageY(pageIndex) {
        var index = root.clamp(
            Math.round(Number(pageIndex || 0)),
            0,
            Math.max(
                0,
                root.multiPageTops.length - 1
            )
        )

        return root.multiPanMarginY
            + Number(
                root.multiPageTops[index]
                || 0
            )
    }

    function multiPageAtDocumentY(documentY) {
        var count =
            root.multiPageHeights.length

        if (count <= 0) {
            return 0
        }

        var y = Number(documentY || 0)

        if (y <= 0) {
            return 0
        }

        for (
            var index = 0;
            index < count;
            index += 1
        ) {
            var top = Number(
                root.multiPageTops[index]
                || 0
            )
            var height = Number(
                root.multiPageHeights[index]
                || 1
            )
            var bottom = top + height

            if (y <= bottom) {
                return index
            }

            if (index < count - 1) {
                var nextTop = Number(
                    root.multiPageTops[
                        index + 1
                    ] || bottom
                )

                if (y < nextTop) {
                    return y - bottom
                        <= nextTop - y
                            ? index
                            : index + 1
                }
            }
        }

        return count - 1
    }

    function multiAnchorAt(
        viewportX,
        viewportY,
        marginX,
        marginY
    ) {
        if (
            !root.documentReady
            || document.pageCount <= 1
            || root.multiPageHeights.length
                !== document.pageCount
        ) {
            return {
                page: 0,
                x: 0.5,
                y: 0.5
            }
        }

        var resolvedMarginX =
            isFinite(Number(marginX))
                ? Number(marginX)
                : root.multiPanMarginX
        var resolvedMarginY =
            isFinite(Number(marginY))
                ? Number(marginY)
                : root.multiPanMarginY
        var contentPointX =
            multiViewport.contentX
            + Number(viewportX || 0)
        var contentPointY =
            multiViewport.contentY
            + Number(viewportY || 0)
        var documentY =
            contentPointY - resolvedMarginY
        var pageIndex =
            root.multiPageAtDocumentY(
                documentY
            )
        var pageWidth = Math.max(
            1,
            Number(
                root.multiPageWidths[
                    pageIndex
                ] || 1
            )
        )
        var pageHeight = Math.max(
            1,
            Number(
                root.multiPageHeights[
                    pageIndex
                ] || 1
            )
        )
        var pageX = resolvedMarginX
            + (
                root.multiDocumentWidth
                - pageWidth
            ) / 2
        var pageY = resolvedMarginY
            + Number(
                root.multiPageTops[
                    pageIndex
                ] || 0
            )

        return {
            page: pageIndex,
            x: root.clamp(
                (
                    contentPointX - pageX
                ) / pageWidth,
                0,
                1
            ),
            y: root.clamp(
                (
                    contentPointY - pageY
                ) / pageHeight,
                0,
                1
            )
        }
    }

    function multiAnchorPosition(
        anchor,
        viewportX,
        viewportY
    ) {
        if (
            !anchor
            || root.multiPageWidths.length <= 0
        ) {
            return {
                x: multiViewport.contentX,
                y: multiViewport.contentY
            }
        }

        var pageIndex = root.clamp(
            Math.round(
                Number(anchor.page || 0)
            ),
            0,
            Math.max(
                0,
                root.multiPageWidths.length - 1
            )
        )
        var pageWidth = Math.max(
            1,
            Number(
                root.multiPageWidths[
                    pageIndex
                ] || 1
            )
        )
        var pageHeight = Math.max(
            1,
            Number(
                root.multiPageHeights[
                    pageIndex
                ] || 1
            )
        )
        var pageX =
            root.multiPageX(pageIndex)
        var pageY =
            root.multiPageY(pageIndex)

        return {
            x: root.clamp(
                pageX
                    + root.clamp(
                        Number(anchor.x),
                        0,
                        1
                    ) * pageWidth
                    - viewportX,
                0,
                root.multiMaximumX
            ),
            y: root.clamp(
                pageY
                    + root.clamp(
                        Number(anchor.y),
                        0,
                        1
                    ) * pageHeight
                    - viewportY,
                0,
                root.multiMaximumY
            )
        }
    }

    function placeMultiAnchor(
        anchor,
        viewportX,
        viewportY
    ) {
        var target = root.multiAnchorPosition(
            anchor,
            viewportX,
            viewportY
        )

        multiViewport.contentX = target.x
        multiViewport.contentY = target.y
    }

    function updateMultiCurrentPage() {
        if (
            !root.documentReady
            || document.pageCount <= 1
            || root.adjustingMultiViewport
        ) {
            return
        }

        var anchor = root.multiAnchorAt(
            multiViewport.width / 2,
            multiViewport.height / 2
        )

        root.multiCurrentPage =
            anchor.page
    }

    function beginMultiVisualTransition() {
        if (!root.documentSurfaceHasRendered) {
            return
        }

        root.multiVisualTransitionActive = true
        multiVisualTransitionTimer.restart()
    }

    function applyMultiZoom(
        value,
        anchorX,
        anchorY,
        usePointerAnchor,
        notifyParent
    ) {
        root.cancelMultiCenterAnimation()

        if (
            !root.documentReady
            || document.pageCount <= 1
        ) {
            return
        }

        var resolvedAnchorX =
            isFinite(Number(anchorX))
                ? root.clamp(
                    Number(anchorX),
                    0,
                    multiViewport.width
                )
                : multiViewport.width / 2
        var resolvedAnchorY =
            isFinite(Number(anchorY))
                ? root.clamp(
                    Number(anchorY),
                    0,
                    multiViewport.height
                )
                : multiViewport.height / 2

        if (!Boolean(usePointerAnchor)) {
            resolvedAnchorX =
                multiViewport.width / 2
            resolvedAnchorY =
                multiViewport.height / 2
        }

        var anchor = root.multiAnchorAt(
            resolvedAnchorX,
            resolvedAnchorY
        )

        if (!root.freePanEnabled) {
            resolvedAnchorX =
                multiViewport.width / 2
            anchor.x = 0.5
        }

        var nextScale =
            root.clampedZoom(value)

        root.beginMultiVisualTransition()
        root.adjustingMultiViewport = true
        root.rebuildMultiLayout(nextScale)
        root.placeMultiAnchor(
            anchor,
            resolvedAnchorX,
            resolvedAnchorY
        )
        root.multiCurrentPage =
            anchor.page
        root.adjustingMultiViewport = false
        root.multiInitialized = true
        root.constrainReadingPosition()

        if (notifyParent) {
            root.requestingZoom = true
            root.zoomFactorRequested(nextScale)
            root.requestingZoom = false
        }

        viewportSaveTimer.restart()
    }

    function multiFitScale() {
        if (
            !root.documentReady
            || document.pageCount <= 1
        ) {
            return 1.0
        }

        var maximumWidth = 1

        for (
            var index = 0;
            index < document.pageCount;
            index += 1
        ) {
            maximumWidth = Math.max(
                maximumWidth,
                document.pagePointSize(
                    index
                ).width
            )
        }

        return root.clampedZoom(
            Math.max(
                1,
                multiViewport.width - 64
            ) / maximumWidth
        )
    }

    function preserveMultiAcrossResize() {
        if (
            !root.documentReady
            || document.pageCount <= 1
            || !root.multiInitialized
        ) {
            root.lastMultiViewportWidth =
                Math.max(
                    1,
                    multiViewport.width
                )
            root.lastMultiViewportHeight =
                Math.max(
                    1,
                    multiViewport.height
                )
            return
        }

        var oldWidth = Math.max(
            1,
            root.lastMultiViewportWidth
        )
        var oldHeight = Math.max(
            1,
            root.lastMultiViewportHeight
        )
        var anchor = root.multiAnchorAt(
            oldWidth / 2,
            oldHeight / 2,
            oldWidth,
            oldHeight
        )

        if (!root.freePanEnabled) {
            anchor.x = 0.5
        }

        root.lastMultiViewportWidth =
            Math.max(
                1,
                multiViewport.width
            )
        root.lastMultiViewportHeight =
            Math.max(
                1,
                multiViewport.height
            )

        root.beginMultiVisualTransition()
        root.rebuildMultiLayout(
            root.fitToWidth
                ? root.multiFitScale()
                : root.multiRenderScale
        )

        var target = root.multiAnchorPosition(
            anchor,
            multiViewport.width / 2,
            multiViewport.height / 2
        )

        root.multiCurrentPage =
            anchor.page

        if (root.freePanEnabled) {
            root.cancelMultiCenterAnimation()
            root.adjustingMultiViewport = true
            multiViewport.contentX = target.x
            multiViewport.contentY = target.y
            root.adjustingMultiViewport = false
            viewportSaveTimer.restart()
        } else {
            var bounds =
                root.readingVerticalBounds()

            root.adjustingMultiViewport = true
            multiViewport.contentY = root.clamp(
                target.y,
                bounds.minimum,
                bounds.maximum
            )
            root.adjustingMultiViewport = false
            root.animateMultiPosition(
                target.x,
                false
            )
            multiResizeSettleTimer.restart()
        }
    }

    function cancelSingleCenterAnimation() {
        singleCenterDelayTimer.stop()
        singleCenterAnimation.stop()
        root.singleCenterAnimating = false
        root.singleCenterChaseY = false
        root.singleCenterVelocityX = 0
        root.singleCenterVelocityY = 0
    }

    function cancelMultiCenterAnimation() {
        multiCenterDelayTimer.stop()
        multiCenterAnimation.stop()
        root.multiCenterAnimating = false
        root.multiCenterVelocityX = 0
    }

    function cancelCenterAnimations() {
        singleResizeSettleTimer.stop()
        multiResizeSettleTimer.stop()
        root.cancelSingleCenterAnimation()
        root.cancelMultiCenterAnimation()
    }

    function resetTransientState() {
        root.cancelCenterAnimations()
        singleResizeTimer.stop()
        multiResizeTimer.stop()
        singlePageRefineTimer.stop()
        multiVisualTransitionTimer.stop()
        viewportSaveTimer.stop()
        root.adjustingSingleViewport = false
        root.adjustingMultiViewport = false
        root.requestingZoom = false
        root.singlePageRefining = false
        root.multiVisualTransitionActive = false
    }

    function startSingleCenterAnimation() {
        singleCenterDelayTimer.stop()

        if (!singleCenterAnimation.running) {
            singleCenterAnimation.start()
        }
    }

    function startMultiCenterAnimation() {
        multiCenterDelayTimer.stop()

        if (!multiCenterAnimation.running) {
            multiCenterAnimation.start()
        }
    }

    function animateSinglePosition(
        targetX,
        targetY,
        delayed,
        chaseY
    ) {
        var resolvedY = root.clamp(
            Number(targetY),
            0,
            root.singleMaximumY
        )

        if (!root.freePanEnabled) {
            var bounds =
                root.readingVerticalBounds()
            resolvedY = root.clamp(
                resolvedY,
                bounds.minimum,
                bounds.maximum
            )
        }

        root.singleCenterTargetX = root.clamp(
            Number(targetX),
            0,
            root.singleMaximumX
        )
        root.singleCenterTargetY = resolvedY
        root.singleCenterChaseY =
            Boolean(chaseY)
        root.singleCenterAnimating = true

        if (!root.singleCenterChaseY) {
            root.singleCenterVelocityY = 0
        }

        if (Boolean(delayed)) {
            singleCenterDelayTimer.restart()
        } else {
            root.startSingleCenterAnimation()
        }
    }

    function animateMultiPosition(
        targetX,
        delayed
    ) {
        root.multiCenterTargetX = root.clamp(
            Number(targetX),
            0,
            root.multiMaximumX
        )
        root.multiCenterAnimating = true

        if (Boolean(delayed)) {
            multiCenterDelayTimer.restart()
        } else {
            root.startMultiCenterAnimation()
        }
    }

    function updateSingleCenterTarget() {
        if (!root.singlePageDocument) {
            return
        }

        root.updateSingleCanvas(
            root.singlePageWidth,
            root.singlePageHeight
        )

        var centered =
            root.singleFocusPosition(
                0.5,
                0.5,
                singleViewport.width / 2,
                singleViewport.height / 2,
                root.singlePageWidth,
                root.singlePageHeight
            )

        root.singleCenterTargetX =
            centered.x

        if (root.singleCenterChaseY) {
            root.singleCenterTargetY =
                centered.y
        } else {
            var bounds =
                root.readingVerticalBounds()
            root.singleCenterTargetY =
                root.clamp(
                    root.singleCenterTargetY,
                    bounds.minimum,
                    bounds.maximum
                )
        }
    }

    function updateMultiCenterTarget() {
        if (
            !root.documentReady
            || document.pageCount <= 1
        ) {
            return
        }

        var pageIndex = root.clamp(
            root.multiCurrentPage,
            0,
            Math.max(
                0,
                document.pageCount - 1
            )
        )
        var centered =
            root.multiAnchorPosition(
                {
                    page: pageIndex,
                    x: 0.5,
                    y: 0.5
                },
                multiViewport.width / 2,
                multiViewport.height / 2
            )
        root.multiCenterTargetX =
            centered.x
    }

    function stepSingleCenterAnimation() {
        if (
            !root.singleCenterAnimating
            || !root.singlePageDocument
        ) {
            root.cancelSingleCenterAnimation()
            return
        }

        root.updateSingleCenterTarget()

        var deltaX =
            root.singleCenterTargetX
            - singleViewport.contentX
        var deltaY =
            root.singleCenterTargetY
            - singleViewport.contentY

        root.singleCenterVelocityX =
            (
                root.singleCenterVelocityX
                + deltaX * root.centerSpringStrength
            ) * root.centerDamping

        if (root.singleCenterChaseY) {
            root.singleCenterVelocityY =
                (
                    root.singleCenterVelocityY
                    + deltaY * root.centerSpringStrength
                ) * root.centerDamping
        } else {
            root.singleCenterVelocityY = 0
            deltaY = 0
        }

        root.adjustingSingleViewport = true
        singleViewport.contentX = root.clamp(
            singleViewport.contentX
                + root.singleCenterVelocityX,
            0,
            root.singleMaximumX
        )

        if (root.singleCenterChaseY) {
            singleViewport.contentY = root.clamp(
                singleViewport.contentY
                    + root.singleCenterVelocityY,
                0,
                root.singleMaximumY
            )
        }
        root.adjustingSingleViewport = false

        var settledX =
            Math.abs(deltaX) < root.centerPositionEpsilon
            && Math.abs(
                root.singleCenterVelocityX
            ) < root.centerVelocityEpsilon
        var settledY =
            !root.singleCenterChaseY
            || (
                Math.abs(deltaY) < root.centerPositionEpsilon
                && Math.abs(
                    root.singleCenterVelocityY
                ) < root.centerVelocityEpsilon
            )

        if (settledX && settledY) {
            root.adjustingSingleViewport = true
            singleViewport.contentX =
                root.singleCenterTargetX

            if (root.singleCenterChaseY) {
                singleViewport.contentY =
                    root.singleCenterTargetY
            }
            root.adjustingSingleViewport = false

            singleCenterAnimation.stop()
            root.singleCenterAnimating = false
            root.singleCenterChaseY = false
            root.singleCenterVelocityX = 0
            root.singleCenterVelocityY = 0
            root.constrainReadingPosition()
            viewportSaveTimer.restart()
        }
    }

    function stepMultiCenterAnimation() {
        if (
            !root.multiCenterAnimating
            || !root.documentReady
            || document.pageCount <= 1
        ) {
            root.cancelMultiCenterAnimation()
            return
        }

        root.updateMultiCenterTarget()

        var deltaX =
            root.multiCenterTargetX
            - multiViewport.contentX

        root.multiCenterVelocityX =
            (
                root.multiCenterVelocityX
                + deltaX * root.centerSpringStrength
            ) * root.centerDamping

        root.adjustingMultiViewport = true
        multiViewport.contentX = root.clamp(
            multiViewport.contentX
                + root.multiCenterVelocityX,
            0,
            root.multiMaximumX
        )
        root.adjustingMultiViewport = false

        var settled =
            Math.abs(deltaX) < root.centerPositionEpsilon
            && Math.abs(
                root.multiCenterVelocityX
            ) < root.centerVelocityEpsilon

        if (settled) {
            root.adjustingMultiViewport = true
            multiViewport.contentX =
                root.multiCenterTargetX
            root.adjustingMultiViewport = false

            multiCenterAnimation.stop()
            root.multiCenterAnimating = false
            root.multiCenterVelocityX = 0
            root.constrainReadingPosition()
            root.updateMultiCurrentPage()
            viewportSaveTimer.restart()
        }
    }

    function settleSingleResizeCenter() {
        if (
            !root.singlePageDocument
            || root.freePanEnabled
        ) {
            return
        }

        root.updateSingleCanvas(
            root.singlePageWidth,
            root.singlePageHeight
        )

        var target = root.singleFocusPosition(
            0.5,
            0.5,
            singleViewport.width / 2,
            singleViewport.height / 2,
            root.singlePageWidth,
            root.singlePageHeight
        )
        var bounds =
            root.readingVerticalBounds()

        root.animateSinglePosition(
            target.x,
            root.clamp(
                singleViewport.contentY,
                bounds.minimum,
                bounds.maximum
            ),
            false
        )
    }

    function settleMultiResizeCenter() {
        if (
            !root.documentReady
            || document.pageCount <= 1
            || root.freePanEnabled
        ) {
            return
        }

        var anchor = root.multiAnchorAt(
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        anchor.x = 0.5

        var target = root.multiAnchorPosition(
            anchor,
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        root.multiCurrentPage = anchor.page
        root.animateMultiPosition(
            target.x,
            false
        )
    }

    function readingVerticalBounds() {
        var viewport = root.singlePageDocument
            ? singleViewport
            : multiViewport
        var documentTop = root.singlePageDocument
            ? root.singlePanMarginY
            : root.multiPanMarginY
        var documentHeight = root.singlePageDocument
            ? root.singlePageHeight
            : root.multiDocumentHeight
        var absoluteMaximum = root.singlePageDocument
            ? root.singleMaximumY
            : root.multiMaximumY
        var edgeInset = root.readingEdgeInset
        var minimumY =
            documentTop - edgeInset
        var maximumY =
            documentTop
            + documentHeight
            - viewport.height
            + edgeInset

        if (maximumY < minimumY) {
            var centeredY =
                documentTop
                + documentHeight / 2
                - viewport.height / 2
            minimumY = centeredY
            maximumY = centeredY
        }

        return {
            minimum: root.clamp(
                minimumY,
                0,
                absoluteMaximum
            ),
            maximum: root.clamp(
                maximumY,
                0,
                absoluteMaximum
            )
        }
    }

    function constrainReadingPosition() {
        if (
            !root.documentReady
            || root.freePanEnabled
        ) {
            return
        }

        var target = root.singlePageDocument
            ? singleViewport
            : multiViewport
        var bounds =
            root.readingVerticalBounds()

        target.contentY = root.clamp(
            target.contentY,
            bounds.minimum,
            bounds.maximum
        )
    }

    function readingScrollDelta(event) {
        var pixelY = Number(
            event.pixelDelta.y || 0
        )

        if (Math.abs(pixelY) > 0.01) {
            return pixelY
        }

        return Number(
            event.angleDelta.y || 0
        ) / 2
    }

    function scrollReadingViewport(event) {
        root.cancelCenterAnimations()

        var target = root.singlePageDocument
            ? singleViewport
            : multiViewport
        var bounds =
            root.readingVerticalBounds()
        var deltaY =
            root.readingScrollDelta(event)

        target.cancelFlick()
        target.contentY = root.clamp(
            target.contentY - deltaY,
            bounds.minimum,
            bounds.maximum
        )

        if (!root.singlePageDocument) {
            root.updateMultiCurrentPage()
        }

        viewportSaveTimer.restart()
    }

    function recenter() {
        if (!root.documentReady) {
            return
        }

        if (root.singlePageDocument) {
            root.updateSingleCanvas(
                root.singlePageWidth,
                root.singlePageHeight
            )

            var singleTarget =
                root.singleFocusPosition(
                    0.5,
                    0.5,
                    singleViewport.width / 2,
                    singleViewport.height / 2,
                    root.singlePageWidth,
                    root.singlePageHeight
                )

            root.singleInitialized = true
            root.animateSinglePosition(
                singleTarget.x,
                singleTarget.y,
                true,
                true
            )
            return
        }

        if (document.pageCount <= 1) {
            return
        }

        var anchor = root.multiAnchorAt(
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        anchor.x = 0.5

        var multiTarget =
            root.multiAnchorPosition(
                anchor,
                multiViewport.width / 2,
                multiViewport.height / 2
            )

        root.multiCurrentPage = anchor.page
        root.multiInitialized = true
        root.animateMultiPosition(
            multiTarget.x,
            true
        )
    }

    function requestZoom(
        value,
        viewportX,
        viewportY
    ) {
        var usePointer =
            isFinite(Number(viewportX))
            && isFinite(Number(viewportY))

        if (root.singlePageDocument) {
            root.applySingleZoom(
                value,
                viewportX,
                viewportY,
                usePointer,
                true
            )
            return
        }

        root.applyMultiZoom(
            value,
            viewportX,
            viewportY,
            usePointer,
            true
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
        root.pendingPage = Math.max(
            0,
            Number(page || 0)
        )
        root.pendingZoom =
            root.clampedZoom(zoom)
        root.pendingFitToWidth =
            Boolean(fitToWidth)
        root.pendingFocusX =
            isFinite(Number(focusX))
                ? root.clamp(
                    Number(focusX),
                    0,
                    1
                )
                : 0.5
        root.pendingFocusY =
            isFinite(Number(focusY))
                ? root.clamp(
                    Number(focusY),
                    0,
                    1
                )
                : 0.5

        if (!root.documentReady) {
            return
        }

        root.applyPendingViewport()
    }

    function applyPendingViewport() {
        if (!root.documentReady) {
            return
        }

        if (root.singlePageDocument) {
            root.adjustingSingleViewport = true
            root.singleRenderScale =
                root.pendingFitToWidth
                    ? root.singleFitScale()
                    : root.pendingZoom
            root.updateSingleCanvas(
                root.singlePageWidth,
                root.singlePageHeight
            )
            root.placeSingleFocus(
                root.freePanEnabled
                    ? root.pendingFocusX
                    : 0.5,
                root.pendingFocusY,
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

        root.adjustingMultiViewport = true
        root.rebuildMultiLayout(
            root.pendingFitToWidth
                ? root.multiFitScale()
                : root.pendingZoom
        )
        root.placeMultiAnchor(
            {
                page: root.clamp(
                    Math.round(
                        root.pendingPage
                    ),
                    0,
                    Math.max(
                        0,
                        document.pageCount - 1
                    )
                ),
                x: root.freePanEnabled
                    ? root.pendingFocusX
                    : 0.5,
                y: root.pendingFocusY
            },
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        root.multiCurrentPage =
            root.clamp(
                Math.round(
                    root.pendingPage
                ),
                0,
                Math.max(
                    0,
                    document.pageCount - 1
                )
            )
        root.adjustingMultiViewport = false
        root.multiInitialized = true
        root.constrainReadingPosition()
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
                false
            )
            viewportSaveTimer.restart()
            return
        }

        var anchor = root.multiAnchorAt(
            multiViewport.width / 2,
            multiViewport.height / 2
        )

        if (!root.freePanEnabled) {
            anchor.x = 0.5
        }

        root.beginMultiVisualTransition()
        root.adjustingMultiViewport = true
        root.rebuildMultiLayout(
            root.multiFitScale()
        )
        root.placeMultiAnchor(
            anchor,
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        root.multiCurrentPage =
            anchor.page
        root.adjustingMultiViewport = false
        root.multiInitialized = true
        root.constrainReadingPosition()
        viewportSaveTimer.restart()
    }

    function emitViewportChanged() {
        if (!root.documentReady) {
            return
        }

        var contentX = 0
        var contentY = 0
        var focus = Qt.point(0.5, 0.5)

        if (root.singlePageDocument) {
            contentX =
                singleViewport.contentX
            contentY =
                singleViewport.contentY
            focus = root.singleFocusAt(
                singleViewport.width / 2,
                singleViewport.height / 2
            )
        } else {
            contentX =
                multiViewport.contentX
            contentY =
                multiViewport.contentY
            focus = root.multiAnchorAt(
                multiViewport.width / 2,
                multiViewport.height / 2
            )
        }

        root.viewportChanged(
            root.currentPage,
            root.activeRenderScale,
            root.fitToWidth,
            contentX,
            contentY,
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
                root.activeRenderScale
                    - root.zoomFactor
            ) > 0.0001
        ) {
            root.requestZoom(root.zoomFactor)
        }
    }

    onFitToWidthChanged: {
        if (
            root.documentReady
            && root.fitToWidth
        ) {
            root.fitWidth()
        }
    }

    onFreePanEnabledChanged: {
        singleViewport.cancelFlick()
        multiViewport.cancelFlick()

        if (
            root.documentReady
            && !root.freePanEnabled
        ) {
            Qt.callLater(root.recenter)
        }
    }

    onSourceChanged: {
        root.resetTransientState()
        root.documentSurfaceHasRendered =
            false
        root.singleInitialized = false
        root.singlePageHasRendered = false
        root.multiInitialized = false
        root.multiCurrentPage = 0
        root.multiPageWidths = []
        root.multiPageHeights = []
        root.multiPageTops = []
        root.pendingPage = 0
        root.pendingZoom =
            root.clampedZoom(root.zoomFactor)
        root.pendingFitToWidth =
            root.fitToWidth
        root.pendingFocusX = 0.5
        root.pendingFocusY = 0.5
    }

    Component.onCompleted: {
        root.lastSingleViewportWidth =
            Math.max(
                1,
                singleViewport.width
            )
        root.lastSingleViewportHeight =
            Math.max(
                1,
                singleViewport.height
            )
        root.lastMultiViewportWidth =
            Math.max(
                1,
                multiViewport.width
            )
        root.lastMultiViewportHeight =
            Math.max(
                1,
                multiViewport.height
            )
        root.singleRenderScale =
            root.clampedZoom(
                root.zoomFactor
            )
        root.multiRenderScale =
            root.clampedZoom(
                root.zoomFactor
            )
    }

    Component.onDestruction:
        root.resetTransientState()

    PdfDocument {
        id: document
        source: root.source

        onStatusChanged: {
            if (
                status
                !== PdfDocument.Ready
            ) {
                return
            }

            if (document.pageCount === 1) {
                root.singleRenderScale =
                    root.pendingFitToWidth
                        ? root.singleFitScale()
                        : root.pendingZoom
            } else {
                root.multiRenderScale =
                    root.pendingFitToWidth
                        ? root.multiFitScale()
                        : root.pendingZoom
            }

            root.applyPendingViewport()
        }
    }

    Component {
        id: multiPageImageComponent

        PdfPageImage {
            anchors.fill: parent
            document: renderDocument
            currentFrame: renderPageIndex
            sourceSize: Qt.size(
                Math.max(
                    1,
                    Math.ceil(renderPageWidth)
                ),
                Math.max(
                    1,
                    Math.ceil(renderPageHeight)
                )
            )
            asynchronous: true
            retainWhileLoading: true
            cache: false
            smooth: true
            mipmap: true
            fillMode: Image.Stretch

            onStatusChanged: {
                if (
                    status === Image.Loading
                    && root.documentSurfaceHasRendered
                ) {
                    root.beginMultiVisualTransition()
                    return
                }

                if (status === Image.Ready) {
                    root.documentSurfaceHasRendered = true
                    multiVisualTransitionTimer.restart()
                }
            }
        }
    }

    Flickable {
        id: singleViewport

        anchors.fill: parent
        visible: root.singlePageDocument
        clip: true
        interactive:
            root.freePanEnabled
        acceptedButtons:
            root.freePanEnabled
                ? Qt.LeftButton | Qt.MiddleButton
                : Qt.NoButton
        pixelAligned: false
        maximumFlickVelocity: 5200
        flickDeceleration: 1900
        contentWidth:
            root.singleCanvasWidth
        contentHeight:
            root.singleCanvasHeight
        boundsBehavior:
            Flickable.StopAtBounds
        flickableDirection:
            Flickable.AutoFlickIfNeeded

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        onWidthChanged: {
            if (!singleResizeTimer.running) {
                singleResizeTimer.start()
            }
            singleResizeSettleTimer.restart()
        }

        onHeightChanged: {
            if (!singleResizeTimer.running) {
                singleResizeTimer.start()
            }
            singleResizeSettleTimer.restart()
        }

        onContentXChanged: {
            if (
                !root.adjustingSingleViewport
                && !root.singleCenterAnimating
            ) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (
                !root.adjustingSingleViewport
                && !root.singleCenterAnimating
            ) {
                viewportSaveTimer.restart()
            }
        }

        onMovementStarted:
            root.cancelSingleCenterAnimation()

        onMovementEnded:
            root.emitViewportChanged()

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy:
                TapHandler.DragThreshold
            onDoubleTapped:
                root.recenter()
        }

        HoverHandler {
            enabled: root.freePanEnabled
            cursorShape:
                singleViewport.dragging
                    ? Qt.ClosedHandCursor
                    : Qt.OpenHandCursor
        }

        Item {
            width:
                singleViewport.contentWidth
            height:
                singleViewport.contentHeight

            Rectangle {
                x: singlePageFrame.x + 8
                y: singlePageFrame.y + 10
                width: singlePageFrame.width
                height: singlePageFrame.height
                visible:
                    singlePageImage.status
                        === Image.Ready
                color: "#6f000000"
                radius: 2
            }

            Rectangle {
                id: singlePageFrame

                x: root.singlePanMarginX
                y: root.singlePanMarginY
                width:
                    root.singlePageWidth
                height:
                    root.singlePageHeight
                color: "white"
                border.width: 1
                border.color:
                    root.theme.panelBorder
                radius: 1
                clip: true

                layer.enabled:
                    root.singlePageRefining
                layer.effect: MultiEffect {
                    autoPaddingEnabled: false
                    blurEnabled: true
                    blurMax: 8
                    blur:
                        root.singlePageRefining
                            ? 0.22
                            : 0
                }

                PdfPageImage {
                    id: singlePageImage

                    anchors.fill: parent
                    document: document
                    currentFrame: 0
                    sourceSize: Qt.size(
                        Math.max(
                            1,
                            Math.ceil(
                                root.singlePageWidth
                            )
                        ),
                        Math.max(
                            1,
                            Math.ceil(
                                root.singlePageHeight
                            )
                        )
                    )
                    asynchronous: true
                    retainWhileLoading: true
                    cache: false
                    smooth: true
                    mipmap: true
                    fillMode: Image.Stretch

                    onStatusChanged: {
                        if (
                            status
                            === Image.Loading
                        ) {
                            if (
                                root.singlePageHasRendered
                            ) {
                                root.singlePageRefining =
                                    true
                            }
                            return
                        }

                        if (
                            status
                            === Image.Ready
                        ) {
                            root.documentSurfaceHasRendered =
                                true
                            root.singlePageHasRendered =
                                true
                            singlePageRefineTimer.restart()
                            return
                        }

                        if (
                            status
                            === Image.Error
                        ) {
                            root.singlePageRefining =
                                false
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: multiViewport

        anchors.fill: parent
        visible:
            root.documentReady
            && document.pageCount > 1
        clip: true
        interactive:
            root.freePanEnabled
        acceptedButtons:
            root.freePanEnabled
                ? Qt.LeftButton | Qt.MiddleButton
                : Qt.NoButton
        pixelAligned: false
        maximumFlickVelocity: 5200
        flickDeceleration: 1900
        contentWidth:
            root.multiCanvasWidth
        contentHeight:
            root.multiCanvasHeight
        boundsBehavior:
            Flickable.StopAtBounds
        flickableDirection:
            Flickable.AutoFlickIfNeeded

        layer.enabled:
            root.multiVisualTransitionActive
        layer.smooth: true
        layer.mipmap: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blurMax: 8
            blur:
                root.multiVisualTransitionActive
                    ? 0.16
                    : 0
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        onWidthChanged: {
            root.beginMultiVisualTransition()

            if (!multiResizeTimer.running) {
                multiResizeTimer.start()
            }
            multiResizeSettleTimer.restart()
        }

        onHeightChanged: {
            root.beginMultiVisualTransition()

            if (!multiResizeTimer.running) {
                multiResizeTimer.start()
            }
            multiResizeSettleTimer.restart()
        }

        onContentXChanged: {
            if (
                !root.adjustingMultiViewport
                && !root.multiCenterAnimating
            ) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (
                !root.adjustingMultiViewport
                && !root.multiCenterAnimating
            ) {
                root.updateMultiCurrentPage()
                viewportSaveTimer.restart()
            }
        }

        onMovementStarted:
            root.cancelMultiCenterAnimation()

        onMovementEnded:
            root.emitViewportChanged()

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy:
                TapHandler.DragThreshold
            onDoubleTapped:
                root.recenter()
        }

        HoverHandler {
            enabled: root.freePanEnabled
            cursorShape:
                multiViewport.dragging
                    ? Qt.ClosedHandCursor
                    : Qt.OpenHandCursor
        }

        Item {
            id: multiCanvas

            width:
                multiViewport.contentWidth
            height:
                multiViewport.contentHeight

            Repeater {
                model:
                    root.documentReady
                        ? document.pageCount
                        : 0

                delegate: Item {
                    id: pageContainer

                    required property int index
                    property int pageIndex: index
                    property real pageWidth:
                        Number(
                            root.multiPageWidths[
                                pageIndex
                            ] || 1
                        )
                    property real pageHeight:
                        Number(
                            root.multiPageHeights[
                                pageIndex
                            ] || 1
                        )

                    x:
                        root.multiPageX(
                            pageIndex
                        )
                    y:
                        root.multiPageY(
                            pageIndex
                        )
                    width: pageWidth
                    height: pageHeight

                    readonly property bool nearViewport:
                        y + height
                            >= multiViewport.contentY
                                - multiViewport.height
                                * 1.5
                        && y
                            <= multiViewport.contentY
                                + multiViewport.height
                                * 2.5

                    Rectangle {
                        x: 8
                        y: 10
                        width: parent.width
                        height: parent.height
                        color: "#6f000000"
                        radius: 2
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "white"
                        border.width: 1
                        border.color:
                            root.theme.panelBorder
                        radius: 1
                        clip: true

                        Loader {
                            id: pageImageLoader

                            anchors.fill: parent
                            active:
                                pageContainer.nearViewport
                            asynchronous: false
                            visible:
                                status === Loader.Ready

                            property var renderDocument:
                                document
                            property int renderPageIndex:
                                pageContainer.pageIndex
                            property real renderPageWidth:
                                pageContainer.pageWidth
                            property real renderPageHeight:
                                pageContainer.pageHeight

                            sourceComponent:
                                multiPageImageComponent
                        }
                    }
                }
            }
        }
    }

    Item {
        id: documentInputLayer

        anchors.fill: parent
        enabled: root.documentReady
        z: 100

        WheelHandler {
            enabled:
                !root.freePanEnabled
            acceptedDevices:
                PointerDevice.Mouse
                | PointerDevice.TouchPad
            acceptedModifiers:
                Qt.NoModifier
            blocking: true

            onWheel: function(event) {
                root.scrollReadingViewport(event)
                event.accepted = true
            }
        }

        WheelHandler {
            acceptedDevices:
                PointerDevice.Mouse
                | PointerDevice.TouchPad
            acceptedModifiers:
                Qt.ControlModifier
            blocking: true

            onWheel: function(event) {
                var direction =
                    event.angleDelta.y >= 0
                        ? 1
                        : -1
                var step =
                    root.activeRenderScale < 0.5
                        ? 0.05
                        : 0.1

                root.requestZoom(
                    root.activeRenderScale
                        + direction * step,
                    point.position.x,
                    point.position.y
                )
                event.accepted = true
            }
        }

        PinchHandler {
            id: documentPinchHandler

            target: null
            acceptedDevices:
                PointerDevice.TouchPad
                | PointerDevice.TouchScreen
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
                        root.pinchStartZoom
                            * activeScale,
                        root.pinchAnchorX,
                        root.pinchAnchorY
                    )
                }
            }
        }
    }

    Timer {
        id: singleCenterDelayTimer

        interval: root.centerDelayInterval
        repeat: false
        onTriggered:
            root.startSingleCenterAnimation()
    }

    Timer {
        id: singleCenterAnimation

        interval: root.centerFrameInterval
        repeat: true
        onTriggered:
            root.stepSingleCenterAnimation()
    }

    Timer {
        id: multiCenterDelayTimer

        interval: root.centerDelayInterval
        repeat: false
        onTriggered:
            root.startMultiCenterAnimation()
    }

    Timer {
        id: multiCenterAnimation

        interval: root.centerFrameInterval
        repeat: true
        onTriggered:
            root.stepMultiCenterAnimation()
    }

    Timer {
        id: singleResizeSettleTimer

        interval: root.resizeSettleInterval
        repeat: false
        onTriggered:
            root.settleSingleResizeCenter()
    }

    Timer {
        id: multiResizeSettleTimer

        interval: root.resizeSettleInterval
        repeat: false
        onTriggered:
            root.settleMultiResizeCenter()
    }

    Timer {
        id: singleResizeTimer

        interval: root.centerFrameInterval
        repeat: false
        onTriggered:
            root.preserveSingleAcrossResize()
    }

    Timer {
        id: multiResizeTimer

        interval: root.centerFrameInterval
        repeat: false
        onTriggered:
            root.preserveMultiAcrossResize()
    }

    Timer {
        id: singlePageRefineTimer

        interval: root.singleRefineInterval
        repeat: false
        onTriggered:
            root.singlePageRefining = false
    }

    Timer {
        id: multiVisualTransitionTimer

        interval: root.multiTransitionInterval
        repeat: false
        onTriggered:
            root.multiVisualTransitionActive =
                false
    }

    Timer {
        id: viewportSaveTimer

        interval: root.viewportSaveInterval
        repeat: false
        onTriggered:
            root.emitViewportChanged()
    }

    Rectangle {
        anchors.fill: parent
        visible:
            document.status
                === PdfDocument.Error
            || (
                root.singlePageDocument
                && singlePageImage.status
                    === Image.Error
            )
        color:
            root.theme.workspaceBgDeep

        Text {
            anchors.centerIn: parent
            width:
                Math.min(
                    520,
                    parent.width - 60
                )
            text:
                document.status
                    === PdfDocument.Error
                && document.error.length > 0
                    ? document.error
                    : "Qt could not render this PDF."
            color: root.theme.danger
            wrapMode: Text.Wrap
            horizontalAlignment:
                Text.AlignHCenter
            font.pixelSize:
                root.theme.typeSize(11)
        }
    }
}
