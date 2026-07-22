import QtQuick

Item {
    id: root

    required property var theme
    required property string content
    property bool compact: false

    readonly property var blocks: parseBlocks(content)

    implicitHeight: contentColumn.implicitHeight
    height: implicitHeight

    function fenceDescriptor(line) {
        let source = String(line)
        let leadingSpaces = 0

        while (
            leadingSpaces < source.length
            && leadingSpaces < 3
            && source.charAt(leadingSpaces) === " "
        ) {
            leadingSpaces += 1
        }

        source = source.slice(leadingSpaces)

        const marker = source.charAt(0)
        if (marker !== "`" && marker !== "~") {
            return null
        }

        let markerLength = 0
        while (source.charAt(markerLength) === marker) {
            markerLength += 1
        }

        if (markerLength < 3) {
            return null
        }

        return {
            marker: marker,
            markerLength: markerLength,
            remainder: source.slice(markerLength)
        }
    }

    function normalizedLanguage(info) {
        const firstToken = String(info).trim().split(/\s+/)[0] || ""
        return firstToken
            .replace(/^\{?\.?/, "")
            .replace(/\}?$/, "")
            .replace(/[^A-Za-z0-9_+#.-]/g, "")
    }

    function trimBoundaryLines(value) {
        return String(value)
            .replace(/^\n+/, "")
            .replace(/\n+$/, "")
    }

    function parseBlocks(value) {
        const lines = String(value).replace(/\r\n?/g, "\n").split("\n")
        const result = []
        let markdownLines = []
        let codeLines = []
        let activeMarker = ""
        let activeMarkerLength = 0
        let language = ""

        function flushMarkdown() {
            const markdown = root.trimBoundaryLines(markdownLines.join("\n"))
            if (markdown.length > 0) {
                result.push({
                    kind: "markdown",
                    content: markdown
                })
            }
            markdownLines = []
        }

        for (let index = 0; index < lines.length; index += 1) {
            const line = lines[index]
            const descriptor = root.fenceDescriptor(line)

            if (activeMarker.length === 0) {
                if (descriptor) {
                    flushMarkdown()
                    activeMarker = descriptor.marker
                    activeMarkerLength = descriptor.markerLength
                    language = root.normalizedLanguage(descriptor.remainder)
                    codeLines = []
                } else {
                    markdownLines.push(line)
                }
                continue
            }

            const closesFence = descriptor
                && descriptor.marker === activeMarker
                && descriptor.markerLength >= activeMarkerLength
                && descriptor.remainder.trim().length === 0

            if (closesFence) {
                result.push({
                    kind: "code",
                    content: codeLines.join("\n"),
                    language: language,
                    complete: true
                })
                activeMarker = ""
                activeMarkerLength = 0
                language = ""
                codeLines = []
            } else {
                codeLines.push(line)
            }
        }

        if (activeMarker.length > 0) {
            result.push({
                kind: "code",
                content: codeLines.join("\n"),
                language: language,
                complete: false
            })
        } else {
            flushMarkdown()
        }

        if (result.length === 0) {
            result.push({
                kind: "markdown",
                content: ""
            })
        }

        return result
    }

    function safeMarkdown(value) {
        let safe = String(value)

        safe = safe.replace(
            /!\[([^\]]*)\]\([^\n)]*\)/g,
            function(match, alt) {
                return alt.length > 0
                    ? "*Image omitted: " + alt + "*"
                    : "*Image omitted*"
            }
        )
        safe = safe.replace(
            /!\[([^\]]*)\]\s*\[[^\]]*\]/g,
            function(match, alt) {
                return alt.length > 0
                    ? "*Image omitted: " + alt + "*"
                    : "*Image omitted*"
            }
        )
        safe = safe.replace(
            /<\s*img\b[^>]*>/gi,
            "&lt;image omitted&gt;"
        )

        return safe
    }

    function openSafeLink(link) {
        const target = String(link).trim()
        const lowerTarget = target.toLowerCase()
        const allowed = lowerTarget.indexOf("https://") === 0
            || lowerTarget.indexOf("http://") === 0
            || lowerTarget.indexOf("mailto:") === 0

        if (allowed) {
            Qt.openUrlExternally(target)
        }
    }

    Column {
        id: contentColumn

        width: parent.width
        spacing: root.compact ? 12 : 16

        Repeater {
            model: root.blocks

            delegate: Item {
                id: blockItem

                required property var modelData

                readonly property bool codeBlock: modelData.kind === "code"

                width: contentColumn.width
                implicitHeight: codeBlock
                    ? codePanel.implicitHeight
                    : markdownText.contentHeight
                height: implicitHeight

                TextEdit {
                    id: markdownText

                    visible: !blockItem.codeBlock
                    width: parent.width
                    height: visible ? contentHeight : 0
                    text: visible
                        ? root.safeMarkdown(String(blockItem.modelData.content || ""))
                        : ""
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.compact ? 14 : 15
                    font.preferTypoLineMetrics: true
                    textFormat: TextEdit.MarkdownText
                    textMargin: 0
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    persistentSelection: true
                    cursorVisible: false
                    selectionColor: root.theme.messageSelectionBg
                    selectedTextColor: root.theme.messageSelectionText
                    renderType: TextEdit.NativeRendering
                    onLinkActivated: function(link) {
                        root.openSafeLink(link)
                    }
                }

                CodeBlock {
                    id: codePanel

                    visible: blockItem.codeBlock
                    width: parent.width
                    theme: root.theme
                    code: blockItem.codeBlock
                        ? String(blockItem.modelData.content || "")
                        : ""
                    language: blockItem.codeBlock
                        ? String(blockItem.modelData.language || "")
                        : ""
                }
            }
        }
    }
}
