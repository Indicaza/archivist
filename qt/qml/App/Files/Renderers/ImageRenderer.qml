import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Archivist.Services 1.0

Item {
    id: root

    required property var theme
    property string libraryRootPath: ""
    property string relativePath: ""
    property real zoomFactor: 1.0
    property bool fitToView: false
    property bool checkerboardVisible: true

    property real renderZoom: 1.0
    property real canvasWidth: 1
    property real canvasHeight: 1
    property real lastViewportWidth: 1
    property real lastViewportHeight: 1
    property bool imageInitialized: false
    property bool adjustingViewport: false
    property bool requestingZoom: false
    property bool visualTransitionActive: false
    property bool imageHasRendered: false
    property real sourcePixelWidth: 1
    property real sourcePixelHeight: 1
    property real pinchStartZoom: 1.0
    property real pinchAnchorX: 0
    property real pinchAnchorY: 0

    readonly property real minimumZoom: 0.05
    readonly property real maximumZoom: 8.0
    readonly property url sourceUrl: MarkdownDocumentBridge.resolveImageUrl(
        root.libraryRootPath,
        "",
        "/" + String(root.relativePath || "")
    )
    readonly property bool decoderReady:
        image.status === Image.Ready
    readonly property bool imageReady:
        root.imageHasRendered
    readonly property real naturalWidth: Math.max(
        1,
        root.sourcePixelWidth
    )
    readonly property real naturalHeight: Math.max(
        1,
        root.sourcePixelHeight
    )
    readonly property real fitScale: root.imageReady
        ? Math.min(
            1.0,
            Math.max(1, viewport.width - 96) / root.naturalWidth,
            Math.max(1, viewport.height - 96) / root.naturalHeight
        )
        : 1.0
    readonly property real effectiveScale: root.fitToView
        ? root.fitScale
        : root.renderZoom
    readonly property int effectivePercent: Math.round(
        root.effectiveScale * 100
    )
    readonly property real renderedWidth: root.imageReady
        ? Math.max(1, root.naturalWidth * root.effectiveScale)
        : 1
    readonly property real renderedHeight: root.imageReady
        ? Math.max(1, root.naturalHeight * root.effectiveScale)
        : 1
    readonly property real panMarginX: Math.max(1, viewport.width)
    readonly property real panMarginY: Math.max(1, viewport.height)
    readonly property real maximumContentX: Math.max(
        0,
        root.canvasWidth - viewport.width
    )
    readonly property real maximumContentY: Math.max(
        0,
        root.canvasHeight - viewport.height
    )
    readonly property real viewportFocusX: root.imageReady
        ? root.focusAt(
            viewport.width / 2,
            viewport.height / 2
        ).x
        : 0.5
    readonly property real viewportFocusY: root.imageReady
        ? root.focusAt(
            viewport.width / 2,
            viewport.height / 2
        ).y
        : 0.5

    signal zoomFactorRequested(
        real value,
        real focusX,
        real focusY
    )
    signal fitRequested()
    signal actualSizeRequested()
    signal viewportChanged(
        real contentX,
        real contentY,
        real focusX,
        real focusY
    )

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function clampedZoom(value) {
        return root.clamp(
            Number(value || 1.0),
            root.minimumZoom,
            root.maximumZoom
        )
    }

    function pointInsideImage(viewportX, viewportY) {
        if (!root.imageReady) {
            return false
        }

        var pageLeft = imageFrame.x - viewport.contentX
        var pageTop = imageFrame.y - viewport.contentY

        return viewportX >= pageLeft
            && viewportX <= pageLeft + root.renderedWidth
            && viewportY >= pageTop
            && viewportY <= pageTop + root.renderedHeight
    }

    function focusAt(viewportX, viewportY) {
        if (!root.imageReady) {
            return Qt.point(0.5, 0.5)
        }

        return Qt.point(
            root.clamp(
                (
                    viewport.contentX
                    + viewportX
                    - imageFrame.x
                ) / Math.max(1, root.renderedWidth),
                0,
                1
            ),
            root.clamp(
                (
                    viewport.contentY
                    + viewportY
                    - imageFrame.y
                ) / Math.max(1, root.renderedHeight),
                0,
                1
            )
        )
    }

    function updateCanvas(pageWidth, pageHeight) {
        root.canvasWidth = Math.max(
            viewport.width,
            root.panMarginX * 2 + pageWidth
        )
        root.canvasHeight = Math.max(
            viewport.height,
            root.panMarginY * 2 + pageHeight
        )
    }

    function placeFocus(
        focusX,
        focusY,
        anchorX,
        anchorY,
        pageWidth,
        pageHeight
    ) {
        var targetX = root.panMarginX
            + root.clamp(Number(focusX), 0, 1) * pageWidth
            - anchorX
        var targetY = root.panMarginY
            + root.clamp(Number(focusY), 0, 1) * pageHeight
            - anchorY

        viewport.contentX = root.clamp(
            targetX,
            0,
            root.maximumContentX
        )
        viewport.contentY = root.clamp(
            targetY,
            0,
            root.maximumContentY
        )
    }

    function beginVisualTransition() {
        if (!root.imageHasRendered) {
            return
        }

        root.visualTransitionActive = true
        visualTransitionTimer.restart()
    }

    function centerImage() {
        if (!root.imageReady) {
            return
        }

        root.adjustingViewport = true
        root.updateCanvas(
            root.renderedWidth,
            root.renderedHeight
        )
        root.placeFocus(
            0.5,
            0.5,
            viewport.width / 2,
            viewport.height / 2,
            root.renderedWidth,
            root.renderedHeight
        )
        root.adjustingViewport = false
        root.imageInitialized = true
        viewportSaveTimer.restart()
    }

    function applyZoom(
        value,
        anchorX,
        anchorY,
        usePointerAnchor,
        notifyParent
    ) {
        if (!root.imageReady) {
            root.renderZoom = root.clampedZoom(value)

            if (notifyParent) {
                root.zoomFactorRequested(
                    root.renderZoom,
                    0.5,
                    0.5
                )
            }
            return
        }

        var resolvedAnchorX = isFinite(Number(anchorX))
            ? root.clamp(Number(anchorX), 0, viewport.width)
            : viewport.width / 2
        var resolvedAnchorY = isFinite(Number(anchorY))
            ? root.clamp(Number(anchorY), 0, viewport.height)
            : viewport.height / 2
        var pointerIsUsable = Boolean(usePointerAnchor)
            && root.pointInsideImage(
                resolvedAnchorX,
                resolvedAnchorY
            )

        if (!pointerIsUsable) {
            resolvedAnchorX = viewport.width / 2
            resolvedAnchorY = viewport.height / 2
        }

        root.beginVisualTransition()

        var focus = root.focusAt(
            resolvedAnchorX,
            resolvedAnchorY
        )
        var nextZoom = root.clampedZoom(value)
        var nextWidth = Math.max(
            1,
            root.naturalWidth * nextZoom
        )
        var nextHeight = Math.max(
            1,
            root.naturalHeight * nextZoom
        )

        root.adjustingViewport = true
        root.renderZoom = nextZoom
        root.updateCanvas(nextWidth, nextHeight)
        root.placeFocus(
            focus.x,
            focus.y,
            resolvedAnchorX,
            resolvedAnchorY,
            nextWidth,
            nextHeight
        )
        root.adjustingViewport = false
        root.imageInitialized = true

        if (notifyParent) {
            root.requestingZoom = true
            root.zoomFactorRequested(
                nextZoom,
                focus.x,
                focus.y
            )
            root.requestingZoom = false
        }

        viewportSaveTimer.restart()
    }

    function requestZoom(value, viewportX, viewportY) {
        var usePointer = isFinite(Number(viewportX))
            && isFinite(Number(viewportY))

        root.applyZoom(
            value,
            viewportX,
            viewportY,
            usePointer,
            true
        )
    }

    function restoreViewport(
        contentX,
        contentY,
        focusX,
        focusY
    ) {
        if (!root.imageReady) {
            return
        }

        root.adjustingViewport = true
        root.renderZoom = root.clampedZoom(root.zoomFactor)
        root.updateCanvas(
            root.renderedWidth,
            root.renderedHeight
        )
        root.placeFocus(
            isFinite(Number(focusX)) ? Number(focusX) : 0.5,
            isFinite(Number(focusY)) ? Number(focusY) : 0.5,
            viewport.width / 2,
            viewport.height / 2,
            root.renderedWidth,
            root.renderedHeight
        )
        root.adjustingViewport = false
        root.imageInitialized = true
        viewportSaveTimer.restart()
    }

    function preserveAcrossResize() {
        if (!root.imageReady || !root.imageInitialized) {
            root.lastViewportWidth = Math.max(1, viewport.width)
            root.lastViewportHeight = Math.max(1, viewport.height)
            return
        }

        root.beginVisualTransition()

        var oldWidth = Math.max(1, root.lastViewportWidth)
        var oldHeight = Math.max(1, root.lastViewportHeight)
        var focusX = root.clamp(
            (
                viewport.contentX
                + oldWidth / 2
                - oldWidth
            ) / Math.max(1, root.renderedWidth),
            0,
            1
        )
        var focusY = root.clamp(
            (
                viewport.contentY
                + oldHeight / 2
                - oldHeight
            ) / Math.max(1, root.renderedHeight),
            0,
            1
        )

        root.lastViewportWidth = Math.max(1, viewport.width)
        root.lastViewportHeight = Math.max(1, viewport.height)
        root.adjustingViewport = true
        root.updateCanvas(
            root.renderedWidth,
            root.renderedHeight
        )
        root.placeFocus(
            focusX,
            focusY,
            viewport.width / 2,
            viewport.height / 2,
            root.renderedWidth,
            root.renderedHeight
        )
        root.adjustingViewport = false
        viewportSaveTimer.restart()
    }

    function emitViewportChanged() {
        if (!root.imageReady || root.adjustingViewport) {
            return
        }

        var focus = root.focusAt(
            viewport.width / 2,
            viewport.height / 2
        )

        root.viewportChanged(
            viewport.contentX,
            viewport.contentY,
            focus.x,
            focus.y
        )
    }

    onZoomFactorChanged: {
        if (
            root.imageReady
            && !root.fitToView
            && !root.requestingZoom
            && Math.abs(root.renderZoom - root.zoomFactor) > 0.0001
        ) {
            root.applyZoom(
                root.zoomFactor,
                viewport.width / 2,
                viewport.height / 2,
                false,
                false
            )
        }
    }

    onFitToViewChanged: {
        if (root.imageReady) {
            root.centerImage()
        }
    }

    onSourceUrlChanged: {
        root.imageInitialized = false
        root.imageHasRendered = false
        root.sourcePixelWidth = 1
        root.sourcePixelHeight = 1
        root.visualTransitionActive = false
    }

    onWidthChanged: root.preserveAcrossResize()
    onHeightChanged: root.preserveAcrossResize()

    Component.onCompleted: {
        root.lastViewportWidth = Math.max(1, viewport.width)
        root.lastViewportHeight = Math.max(1, viewport.height)
        root.renderZoom = root.clampedZoom(root.zoomFactor)
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.workspaceBgDeep
    }

    Flickable {
        id: viewport

        anchors.fill: parent
        clip: true
        interactive: root.imageReady
        acceptedButtons: Qt.NoButton
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: root.canvasWidth
        contentHeight: root.canvasHeight

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        onContentXChanged: {
            if (!root.adjustingViewport) {
                viewportSaveTimer.restart()
            }
        }

        onContentYChanged: {
            if (!root.adjustingViewport) {
                viewportSaveTimer.restart()
            }
        }

        onMovementEnded: root.emitViewportChanged()

        Item {
            id: canvas

            width: viewport.contentWidth
            height: viewport.contentHeight

            Rectangle {
                x: imageFrame.x + 7
                y: imageFrame.y + 9
                width: imageFrame.width
                height: imageFrame.height
                visible: root.imageReady
                color: "#72000000"
                radius: 3
            }

            Rectangle {
                id: imageFrame

                x: root.panMarginX
                y: root.panMarginY
                width: root.renderedWidth
                height: root.renderedHeight
                visible: root.imageReady
                color: root.checkerboardVisible
                    ? "transparent"
                    : root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.panelBorder
                radius: 2
                clip: true

                layer.enabled: root.visualTransitionActive
                layer.effect: MultiEffect {
                    autoPaddingEnabled: false
                    blurEnabled: true
                    blurMax: 8
                    blur: root.visualTransitionActive ? 0.18 : 0
                }

                Image {
                    anchors.fill: parent
                    visible: root.checkerboardVisible
                    source: "Assets/checkerboard.svg"
                    sourceSize: Qt.size(32, 32)
                    fillMode: Image.Tile
                    smooth: false
                    cache: true
                }

                Image {
                    id: image

                    anchors.fill: parent
                    source: root.sourceUrl
                    asynchronous: true
                    retainWhileLoading: true
                    cache: true
                    smooth: true
                    mipmap: true
                    fillMode: Image.PreserveAspectFit
                    autoTransform: true

                    onStatusChanged: {
                        if (status === Image.Loading) {
                            if (root.imageHasRendered) {
                                root.beginVisualTransition()
                            }
                            return
                        }

                        if (status === Image.Ready) {
                            root.sourcePixelWidth = Math.max(
                                1,
                                sourceSize.width
                            )
                            root.sourcePixelHeight = Math.max(
                                1,
                                sourceSize.height
                            )

                            if (!root.imageHasRendered) {
                                root.imageHasRendered = true
                                root.renderZoom = root.clampedZoom(
                                    root.zoomFactor
                                )
                                root.centerImage()
                            } else {
                                visualTransitionTimer.restart()
                            }
                            return
                        }

                        if (status === Image.Error) {
                            root.visualTransitionActive = false
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton

                    onDoubleTapped: {
                        if (root.fitToView) {
                            root.actualSizeRequested()
                        } else {
                            root.fitRequested()
                        }
                    }
                }
            }
        }
    }

    WheelHandler {
        acceptedDevices:
            PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier
        blocking: true

        onWheel: function(event) {
            var direction = event.angleDelta.y >= 0 ? 1 : -1
            var step = root.effectiveScale < 0.5 ? 0.05 : 0.1

            root.requestZoom(
                root.effectiveScale + direction * step,
                point.position.x,
                point.position.y
            )
            event.accepted = true
        }
    }

    PinchHandler {
        target: null
        acceptedDevices:
            PointerDevice.TouchPad | PointerDevice.TouchScreen
        scaleAxis.enabled: true

        onActiveChanged: {
            if (active) {
                root.pinchStartZoom = root.effectiveScale
                root.pinchAnchorX = centroid.position.x
                root.pinchAnchorY = centroid.position.y
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

    Timer {
        id: visualTransitionTimer

        interval: 85
        repeat: false
        onTriggered: root.visualTransitionActive = false
    }

    Timer {
        id: viewportSaveTimer

        interval: 80
        repeat: false
        onTriggered: root.emitViewportChanged()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 14
        width: metadataText.implicitWidth + 20
        height: 30
        visible: root.imageReady
        color: "#d923211e"
        border.width: 1
        border.color: root.theme.quietBorder
        radius: 4

        Text {
            id: metadataText

            anchors.centerIn: parent
            text: String(Math.round(root.naturalWidth))
                + " × "
                + String(Math.round(root.naturalHeight))
                + " px  ·  "
                + String(root.effectivePercent)
                + "%"
            color: root.theme.appText
            font.family: root.theme.monospaceFontFamily
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.DemiBold
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 64)
        spacing: 9
        visible: (
                !root.imageHasRendered
                && image.status === Image.Loading
            )
            || image.status === Image.Error
            || String(root.sourceUrl).length === 0

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.imageHasRendered
                && image.status === Image.Loading
            running: visible
        }

        Text {
            width: parent.width
            text: image.status === Image.Loading
                ? "Loading image…"
                : "Image unavailable"
            color: image.status === Image.Error
                || String(root.sourceUrl).length === 0
                ? root.theme.danger
                : root.theme.appText
            font.pixelSize: root.theme.typeSize(15)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            visible: image.status !== Image.Loading
            text: String(root.sourceUrl).length === 0
                ? "Archivist could not resolve this file inside the active Library."
                : "Qt could not decode this image format."
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(10)
            lineHeight: root.theme.typeLineHeightBody
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
