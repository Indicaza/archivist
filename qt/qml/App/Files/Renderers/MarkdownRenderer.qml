import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0

ScrollView {
    id: root

    required property var theme
    property string content: ""
    property string libraryRootPath: ""
    property string documentRelativePath: ""
    property real zoomFactor: 1.0
    property real pinchStartZoom: 1.0

    readonly property real minimumZoom: 0.65
    readonly property real maximumZoom: 2.0
    readonly property real paperWidth: 816
    readonly property real outerMargin: 48
    readonly property real pageHorizontalPadding: 72
    readonly property real pageVerticalPadding: 64

    signal zoomFactorRequested(real value)

    clip: true
    contentWidth: documentCanvas.width
    contentHeight: documentCanvas.height
    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    function clampedZoom(value) {
        return Math.max(root.minimumZoom, Math.min(root.maximumZoom, value))
    }

    function requestZoom(value) {
        root.zoomFactorRequested(root.clampedZoom(value))
    }

    function extractImageTarget(value) {
        var target = String(value || "").trim()

        if (target.charAt(0) === "<") {
            var closingBracket = target.indexOf(">")
            return closingBracket > 0 ? target.slice(1, closingBracket).trim() : ""
        }

        var whitespace = target.search(/\s/)
        return whitespace >= 0 ? target.slice(0, whitespace) : target
    }

    function imageReference(alt, target) {
        var label = String(alt || "").trim()
        var source = String(target || "").trim()

        return "\n> **Image unavailable:** "
            + (label.length > 0 ? label : "Untitled image")
            + (source.length > 0 ? "  \n> `" + source + "`" : "")
            + "\n"
    }

    function resolvedImageMarkdown(alt, target) {
        var source = root.extractImageTarget(target)
        var resolved = MarkdownDocumentBridge.resolveImageUrl(
            root.libraryRootPath,
            root.documentRelativePath,
            source
        )
        var resolvedText = String(resolved || "")

        if (resolvedText.length === 0) {
            return root.imageReference(alt, source)
        }

        return "![" + String(alt || "") + "](<" + resolvedText + ">)"
    }

    function htmlAttribute(tag, name) {
        var quoted = new RegExp(
            "\\b" + name + "\\s*=\\s*([\"'])(.*?)\\1",
            "i"
        ).exec(tag)

        if (quoted && quoted.length > 2) {
            return quoted[2]
        }

        var bare = new RegExp("\\b" + name + "\\s*=\\s*([^\\s>]+)", "i").exec(tag)
        return bare && bare.length > 1 ? bare[1] : ""
    }

    function renderableMarkdown(value) {
        var markdown = String(value || "")
        var references = ({})

        markdown.replace(
            /^\s*\[([^\]]+)\]:\s*(?:<([^>]+)>|([^\s]+))(?:\s+.*)?$/gm,
            function(_, identifier, angleTarget, bareTarget) {
                references[String(identifier || "").trim().toLowerCase()] = String(
                    angleTarget || bareTarget || ""
                ).trim()
                return _
            }
        )

        markdown = markdown.replace(
            /!\[([^\]]*)\]\((<[^>\n]+>|[^)\n]+)\)/g,
            function(_, alt, target) {
                return root.resolvedImageMarkdown(alt, target)
            }
        )

        markdown = markdown.replace(
            /!\[([^\]]*)\]\[([^\]]*)\]/g,
            function(_, alt, identifier) {
                var referenceId = String(identifier || alt || "").trim().toLowerCase()
                return root.resolvedImageMarkdown(alt, references[referenceId] || "")
            }
        )

        markdown = markdown.replace(
            /<\s*img\b[^>]*>/gi,
            function(tag) {
                return root.resolvedImageMarkdown(
                    root.htmlAttribute(tag, "alt"),
                    root.htmlAttribute(tag, "src")
                )
            }
        )

        return markdown
    }

    function scheduleImageLayout() {
        imageLayoutTimer.restart()
    }

    onContentChanged: scheduleImageLayout()
    onLibraryRootPathChanged: scheduleImageLayout()
    onDocumentRelativePathChanged: scheduleImageLayout()
    onZoomFactorChanged: scheduleImageLayout()
    Component.onCompleted: scheduleImageLayout()

    PinchHandler {
        id: pinchHandler

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
            root.requestZoom(root.zoomFactor + direction * 0.1)
            event.accepted = true
        }
    }

    Timer {
        id: imageLayoutTimer

        interval: 0
        repeat: false
        onTriggered: MarkdownDocumentBridge.constrainImages(
            markdownText.textDocument,
            markdownText.width
        )
    }

    Item {
        id: documentCanvas

        width: Math.max(root.availableWidth, paper.width + root.outerMargin * 2)
        height: Math.max(root.availableHeight, paper.height + root.outerMargin * 2)

        Rectangle {
            x: paper.x + 6
            y: paper.y + 8
            width: paper.width
            height: paper.height
            color: "#70000000"
            radius: paper.radius + 1
            z: 0
        }

        Rectangle {
            id: paper

            anchors.top: parent.top
            anchors.topMargin: root.outerMargin
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.paperWidth * root.zoomFactor
            height: Math.max(
                root.availableHeight - root.outerMargin * 2,
                markdownText.contentHeight + root.pageVerticalPadding * 2 * root.zoomFactor
            )
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.panelBorder
            radius: 3
            z: 1

            TextEdit {
                id: markdownText

                anchors.top: parent.top
                anchors.topMargin: root.pageVerticalPadding * root.zoomFactor
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(
                    220,
                    parent.width - root.pageHorizontalPadding * 2 * root.zoomFactor
                )
                height: contentHeight
                text: root.renderableMarkdown(root.content)
                textFormat: TextEdit.MarkdownText
                readOnly: true
                selectByMouse: true
                selectByKeyboard: true
                persistentSelection: true
                cursorVisible: false
                wrapMode: TextEdit.Wrap
                color: root.theme.appText
                selectionColor: root.theme.messageSelectionBg
                selectedTextColor: root.theme.messageSelectionText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeBody * root.zoomFactor
                font.weight: Font.Normal
                font.letterSpacing: 0.08
                font.kerning: true
                font.contextFontMerging: true
                font.hintingPreference: Font.PreferNoHinting
                font.preferTypoLineMetrics: true
                renderType: TextEdit.NativeRendering
                onWidthChanged: root.scheduleImageLayout()
                onLinkActivated: function(link) {
                    var target = String(link || "").trim()
                    var lowered = target.toLowerCase()
                    var allowed = lowered.indexOf("https://") === 0
                        || lowered.indexOf("http://") === 0
                        || lowered.indexOf("mailto:") === 0

                    if (allowed) {
                        Qt.openUrlExternally(target)
                    }
                }
            }
        }
    }
}
