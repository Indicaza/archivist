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

    function placeSingleFocus(
        focusX,
        focusY,
        anchorX,
        anchorY,
        pageWidth,
        pageHeight
    ) {
        var targetX = root.singlePanMarginX
            + root.clamp(
                Number(focusX),
                0,
                1
            ) * pageWidth
            - anchorX
        var targetY = root.singlePanMarginY
            + root.clamp(
                Number(focusY),
                0,
                1
            ) * pageHeight
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

    function placeMultiAnchor(
        anchor,
        viewportX,
        viewportY
    ) {
        if (
            !anchor
            || root.multiPageWidths.length <= 0
        ) {
            return
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
        var targetX = pageX
            + root.clamp(
                Number(anchor.x),
                0,
                1
            ) * pageWidth
            - viewportX
        var targetY = pageY
            + root.clamp(
                Number(anchor.y),
                0,
                1
            ) * pageHeight
            - viewportY

        multiViewport.contentX = root.clamp(
            targetX,
            0,
            root.multiMaximumX
        )
        multiViewport.contentY = root.clamp(
            targetY,
            0,
            root.multiMaximumY
        )
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

    function centerMultiDocument() {
        if (
            !root.documentReady
            || document.pageCount <= 1
        ) {
            return
        }

        root.adjustingMultiViewport = true
        root.rebuildMultiLayout(
            root.multiRenderScale
        )
        root.placeMultiAnchor(
            {
                page: 0,
                x: 0.5,
                y: 0
            },
            multiViewport.width / 2,
            32
        )
        root.multiCurrentPage = 0
        root.adjustingMultiViewport = false
        root.multiInitialized = true
        viewportSaveTimer.restart()
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
        root.adjustingMultiViewport = true
        root.rebuildMultiLayout(
            root.multiRenderScale
        )
        root.placeMultiAnchor(
            anchor,
            multiViewport.width / 2,
            multiViewport.height / 2
        )
        root.multiCurrentPage =
            anchor.page
        root.adjustingMultiViewport = false
        viewportSaveTimer.restart()
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
                root.pendingFocusX,
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
                x: root.pendingFocusX,
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

    onSourceChanged: {
        root.documentSurfaceHasRendered =
            false
        root.singleInitialized = false
        root.singlePageHasRendered = false
        root.singlePageRefining = false
        root.multiInitialized = false
        root.multiVisualTransitionActive =
            false
        root.multiCurrentPage = 0
        root.multiPageWidths = []
        root.multiPageHeights = []
        root.multiPageTops = []
        root.pendingPage = 0
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
        acceptedButtons:
            Qt.LeftButton | Qt.MiddleButton
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

        onWidthChanged:
            singleResizeTimer.restart()
        onHeightChanged:
            singleResizeTimer.restart()

        onContentXChanged: {
            if (
                !root.adjustingSingleViewport
            ) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (
                !root.adjustingSingleViewport
            ) {
                viewportSaveTimer.restart()
            }
        }

        onMovementEnded:
            root.emitViewportChanged()

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
        acceptedButtons:
            Qt.LeftButton | Qt.MiddleButton
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
            multiResizeTimer.restart()
        }

        onHeightChanged: {
            root.beginMultiVisualTransition()
            multiResizeTimer.restart()
        }

        onContentXChanged: {
            if (
                !root.adjustingMultiViewport
            ) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (
                !root.adjustingMultiViewport
            ) {
                root.updateMultiCurrentPage()
                viewportSaveTimer.restart()
            }
        }

        onMovementEnded:
            root.emitViewportChanged()

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
        id: singleResizeTimer

        interval: 16
        repeat: false
        onTriggered:
            root.preserveSingleAcrossResize()
    }

    Timer {
        id: multiResizeTimer

        interval: 16
        repeat: false
        onTriggered:
            root.preserveMultiAcrossResize()
    }

    Timer {
        id: singlePageRefineTimer

        interval: 70
        repeat: false
        onTriggered:
            root.singlePageRefining = false
    }

    Timer {
        id: multiVisualTransitionTimer

        interval: 115
        repeat: false
        onTriggered:
            root.multiVisualTransitionActive =
                false
    }

    Timer {
        id: viewportSaveTimer

        interval: 90
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
