import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0

Item {
    id: root

    required property var theme
    property string libraryRootPath: ""
    property string relativePath: ""
    property real zoomFactor: 1.0
    property bool fitToView: true
    property bool checkerboardVisible: true
    property real pinchStartZoom: 1.0

    readonly property real minimumZoom: 0.05
    readonly property real maximumZoom: 8.0
    readonly property real viewportPadding: 48
    readonly property url sourceUrl: MarkdownDocumentBridge.resolveImageUrl(
        root.libraryRootPath,
        "",
        "/" + String(root.relativePath || "")
    )
    readonly property bool imageReady: image.status === Image.Ready
    readonly property real naturalWidth: imageReady
        ? Math.max(1, image.sourceSize.width)
        : 1
    readonly property real naturalHeight: imageReady
        ? Math.max(1, image.sourceSize.height)
        : 1
    readonly property real availableImageWidth: Math.max(
        1,
        viewport.width - root.viewportPadding * 2
    )
    readonly property real availableImageHeight: Math.max(
        1,
        viewport.height - root.viewportPadding * 2
    )
    readonly property real fitScale: imageReady
        ? Math.min(
            1.0,
            root.availableImageWidth / root.naturalWidth,
            root.availableImageHeight / root.naturalHeight
        )
        : 1.0
    readonly property real effectiveScale: (
        root.fitToView ? root.fitScale : 1.0
    ) * root.zoomFactor
    readonly property int effectivePercent: Math.round(root.effectiveScale * 100)

    signal zoomFactorRequested(real value)
    signal fitRequested()
    signal actualSizeRequested()

    function clampedZoom(value) {
        return Math.max(root.minimumZoom, Math.min(root.maximumZoom, value))
    }

    function requestZoom(value) {
        root.zoomFactorRequested(root.clampedZoom(value))
    }

    function centerContent() {
        Qt.callLater(function() {
            viewport.contentX = Math.max(
                0,
                (viewport.contentWidth - viewport.width) / 2
            )
            viewport.contentY = Math.max(
                0,
                (viewport.contentHeight - viewport.height) / 2
            )
        })
    }

    onSourceUrlChanged: centerContent()
    onEffectiveScaleChanged: centerContent()
    onWidthChanged: {
        if (root.fitToView) {
            centerContent()
        }
    }
    onHeightChanged: {
        if (root.fitToView) {
            centerContent()
        }
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
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: Math.max(
            width,
            imageFrame.width + root.viewportPadding * 2
        )
        contentHeight: Math.max(
            height,
            imageFrame.height + root.viewportPadding * 2
        )
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: canvas

            width: viewport.contentWidth
            height: viewport.contentHeight

            Rectangle {
                id: imageShadow

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

                x: Math.max(
                    root.viewportPadding,
                    (canvas.width - width) / 2
                )
                y: Math.max(
                    root.viewportPadding,
                    (canvas.height - height) / 2
                )
                width: root.imageReady
                    ? Math.max(1, root.naturalWidth * root.effectiveScale)
                    : 0
                height: root.imageReady
                    ? Math.max(1, root.naturalHeight * root.effectiveScale)
                    : 0
                visible: root.imageReady
                color: root.checkerboardVisible
                    ? "transparent"
                    : root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.panelBorder
                radius: 2
                clip: true

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
                    cache: true
                    smooth: true
                    mipmap: root.effectiveScale < 1.0
                    fillMode: Image.PreserveAspectFit
                    autoTransform: true
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

    PinchHandler {
        target: null
        acceptedDevices: PointerDevice.TouchPad | PointerDevice.TouchScreen
        scaleAxis.enabled: true

        onActiveChanged: {
            if (active) {
                root.pinchStartZoom = root.zoomFactor
            }
        }

        onActiveScaleChanged: {
            if (active) {
                root.requestZoom(root.pinchStartZoom * activeScale)
            }
        }
    }

    WheelHandler {
        acceptedModifiers: Qt.ControlModifier

        onWheel: function(event) {
            var direction = event.angleDelta.y >= 0 ? 1 : -1
            var step = root.zoomFactor < 0.5 ? 0.05 : 0.1
            root.requestZoom(root.zoomFactor + direction * step)
            event.accepted = true
        }
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
        visible: image.status === Image.Loading
            || image.status === Image.Error
            || String(root.sourceUrl).length === 0

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: image.status === Image.Loading
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
