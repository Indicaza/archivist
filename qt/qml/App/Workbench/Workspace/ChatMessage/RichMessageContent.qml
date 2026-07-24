import QtQuick

Item {
    id: root

    required property var theme
    required property string content
    property bool compact: false
    property bool animateReveal: false
    property int revealPosition: 0
    property real revealAccumulator: 0
    property bool revealing: false
    property bool revealInitialized: false

    readonly property var blocks: parseBlocks(content)
    readonly property int totalRevealLength: blocks.length > 0
        ? Number(blocks[blocks.length - 1].revealEnd || 0)
        : 0

    signal revealProgressed()
    signal revealFinished()

    implicitHeight: contentColumn.implicitHeight
    height: implicitHeight

    onImplicitHeightChanged: {
        if (animateReveal) {
            revealProgressed()
        }
    }

    function configureReveal() {
        revealTimer.stop()
        revealFinishTimer.stop()
        revealAccumulator = 0

        if (animateReveal && totalRevealLength > 0) {
            revealPosition = 0
            revealing = true
            revealTimer.start()
        } else {
            revealPosition = totalRevealLength
            revealing = false

            if (animateReveal) {
                revealFinishTimer.start()
            }
        }

        revealInitialized = true
    }

    function safeSlice(value, requestedLength) {
        const source = String(value)
        let boundary = Math.max(
            0,
            Math.min(source.length, requestedLength)
        )

        if (
            boundary > 0
            && boundary < source.length
            && source.charCodeAt(boundary - 1) >= 0xD800
            && source.charCodeAt(boundary - 1) <= 0xDBFF
            && source.charCodeAt(boundary) >= 0xDC00
            && source.charCodeAt(boundary) <= 0xDFFF
        ) {
            boundary += 1
        }

        return source.slice(0, boundary)
    }

    function spawnCometGlyph(parentItem, textItem, position, order) {
        const firstUnit = textItem.getText(position, position + 1)
        const firstCode = firstUnit.length > 0
            ? firstUnit.charCodeAt(0)
            : 0

        if (
            firstCode >= 0xDC00
            && firstCode <= 0xDFFF
            && position > 0
        ) {
            const previousUnit = textItem.getText(position - 1, position)
            const previousCode = previousUnit.length > 0
                ? previousUnit.charCodeAt(0)
                : 0

            if (previousCode >= 0xD800 && previousCode <= 0xDBFF) {
                return
            }
        }

        const glyph = firstCode >= 0xD800 && firstCode <= 0xDBFF
            ? textItem.getText(position, position + 2)
            : firstUnit

        if (glyph.trim().length === 0) {
            return
        }

        const glyphRect = textItem.positionToRectangle(position)
        const lineHeightRatio = root.compact
            ? theme.typeLineHeightBody
            : theme.typeLineHeightReading
        const glyphSize = Math.min(
            30,
            Math.max(
                textItem.font.pixelSize,
                glyphRect.height / lineHeightRatio
            )
        )
        const visualY = glyphRect.y + Math.max(
            0,
            (glyphRect.height - glyphSize * 1.24) / 2
        )

        cometGlyphComponent.createObject(parentItem, {
            glyph: glyph,
            glyphFontFamily: textItem.font.family,
            glyphFontSize: glyphSize,
            hotColor: theme.cometHotText,
            warmColor: theme.cometWarmText,
            settledColor: textItem.color,
            settledX: glyphRect.x,
            settledY: visualY,
            rise: theme.chatCometRise,
            startScale: theme.chatCometStartScale,
            settleDuration: theme.chatCometSettleDuration,
            coolingDuration: theme.chatCometCoolingDuration,
            birthDelay: Math.min(56, order * 8)
        })
    }

    Component.onCompleted: Qt.callLater(function() {
        root.configureReveal()
    })

    onAnimateRevealChanged: {
        if (revealInitialized) {
            configureReveal()
        }
    }

    onContentChanged: {
        if (revealInitialized) {
            configureReveal()
        }
    }

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

    function thematicBreak(line) {
        return /^ {0,3}(?:(?:\*[ \t]*){3,}|(?:_[ \t]*){3,}|(?:-[ \t]*){3,})$/.test(
            String(line)
        )
    }

    function markdownTableDelimiter(line) {
        let source = String(line).trim()

        if (source.indexOf("|") < 0) {
            return false
        }

        let cells = source.split("|")

        if (cells.length > 0 && cells[0].trim().length === 0) {
            cells.shift()
        }

        if (
            cells.length > 0
            && cells[cells.length - 1].trim().length === 0
        ) {
            cells.pop()
        }

        if (cells.length < 2) {
            return false
        }

        for (let index = 0; index < cells.length; index += 1) {
            if (!/^:?-{3,}:?$/.test(cells[index].trim())) {
                return false
            }
        }

        return true
    }

    function markdownTableRow(line) {
        const source = String(line).trim()
        return source.length > 0 && source.indexOf("|") >= 0
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
            let sectionLines = []

            function flushSection() {
                let proseLines = []

                function pushMarkdown(lines, structured) {
                    const markdown = root.trimBoundaryLines(lines.join("\n"))

                    if (markdown.length === 0) {
                        return
                    }

                    const rowCount = markdown.split("\n").length

                    result.push({
                        kind: structured
                            ? "structuredMarkdown"
                            : "markdown",
                        content: markdown,
                        revealLength: structured
                            ? Math.min(48, Math.max(16, rowCount * 6))
                            : markdown.length
                    })
                }

                function flushProse() {
                    pushMarkdown(proseLines, false)
                    proseLines = []
                }

                for (let index = 0; index < sectionLines.length; index += 1) {
                    const beginsTable = index + 1 < sectionLines.length
                        && root.markdownTableRow(sectionLines[index])
                        && root.markdownTableDelimiter(
                            sectionLines[index + 1]
                        )

                    if (!beginsTable) {
                        proseLines.push(sectionLines[index])
                        continue
                    }

                    flushProse()

                    const tableLines = [
                        sectionLines[index],
                        sectionLines[index + 1]
                    ]
                    index += 2

                    while (
                        index < sectionLines.length
                        && root.markdownTableRow(sectionLines[index])
                    ) {
                        tableLines.push(sectionLines[index])
                        index += 1
                    }

                    index -= 1
                    pushMarkdown(tableLines, true)
                }

                flushProse()
                sectionLines = []
            }

            for (let index = 0; index < markdownLines.length; index += 1) {
                const line = markdownLines[index]
                const previousBlank = index === 0
                    || markdownLines[index - 1].trim().length === 0
                const nextBlank = index === markdownLines.length - 1
                    || markdownLines[index + 1].trim().length === 0

                if (root.thematicBreak(line) && previousBlank && nextBlank) {
                    flushSection()
                    result.push({ kind: "divider" })
                } else {
                    sectionLines.push(line)
                }
            }

            flushSection()
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

        let revealOffset = 0

        for (let index = 0; index < result.length; index += 1) {
            const block = result[index]
            const revealLength = block.revealLength !== undefined
                ? Math.max(1, Number(block.revealLength))
                : block.kind === "divider"
                    ? 1
                    : String(block.content || "").length

            block.revealStart = revealOffset
            block.revealEnd = revealOffset + revealLength
            revealOffset += revealLength
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

    function displayMarkdown(value) {
        return safeMarkdown(value).replace(
            /^(#{1,2})([ \t]+)/gm,
            "#$1$2"
        )
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
        spacing: root.compact ? 16 : 30

        Repeater {
            model: root.blocks

            delegate: Item {
                id: blockItem

                required property var modelData

                readonly property bool codeBlock: modelData.kind === "code"
                readonly property bool dividerBlock: modelData.kind === "divider"
                readonly property bool structuredBlock:
                    modelData.kind === "structuredMarkdown"
                readonly property int timelineLength: Math.max(
                    0,
                    Number(modelData.revealEnd || 0)
                        - Number(modelData.revealStart || 0)
                )
                readonly property int contentLength: String(
                    modelData.content || ""
                ).length
                readonly property int visibleCharacters: root.animateReveal
                    ? structuredBlock
                        ? root.revealPosition
                            > Number(modelData.revealStart || 0)
                            ? contentLength
                            : 0
                        : Math.max(
                            0,
                            Math.min(
                                contentLength,
                                root.revealPosition
                                    - Number(modelData.revealStart || 0)
                            )
                        )
                    : contentLength
                readonly property bool revealed: !root.animateReveal
                    || visibleCharacters > 0
                    || (
                        timelineLength === 0
                        && root.revealPosition
                            >= Number(modelData.revealStart || 0)
                    )

                width: contentColumn.width
                visible: revealed
                implicitHeight: !visible
                    ? 0
                    : codeBlock
                    ? codePanel.implicitHeight
                    : dividerBlock
                        ? 1
                        : markdownText.contentHeight
                height: implicitHeight
                opacity: structuredBlock && !revealed ? 0 : 1

                Behavior on opacity {
                    enabled: blockItem.structuredBlock

                    NumberAnimation {
                        duration: 220
                        easing.type: Easing.OutCubic
                    }
                }

                TextEdit {
                    id: markdownText

                    visible: !blockItem.codeBlock && !blockItem.dividerBlock
                    width: parent.width
                    height: visible ? contentHeight : 0
                    text: visible
                        ? root.displayMarkdown(
                            root.safeSlice(
                                String(blockItem.modelData.content || ""),
                                blockItem.visibleCharacters
                            )
                        )
                        : ""
                    color: root.theme.appText
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.typeBody
                    font.weight: Font.Normal
                    font.letterSpacing: 0.08
                    font.kerning: true
                    font.contextFontMerging: true
                    font.hintingPreference: Font.PreferNoHinting
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

                    property int observedLength: 0
                    readonly property bool animateGlyphs:
                        root.animateReveal && !blockItem.structuredBlock

                    onTextChanged: glyphCapture.restart()

                    Component.onCompleted: {
                        observedLength = animateGlyphs ? 0 : length

                        if (animateGlyphs && length > 0) {
                            glyphCapture.restart()
                        }
                    }

                    Timer {
                        id: glyphCapture

                        interval: 0
                        repeat: false
                        onTriggered: {
                            const currentLength = markdownText.length

                            if (!markdownText.animateGlyphs) {
                                markdownText.observedLength = currentLength
                                return
                            }

                            if (currentLength <= markdownText.observedLength) {
                                markdownText.observedLength = currentLength
                                return
                            }

                            const firstPosition = Math.max(
                                markdownText.observedLength,
                                currentLength
                                    - root.theme.chatCometMaximumGlyphsPerFrame
                            )

                            for (
                                let position = firstPosition;
                                position < currentLength;
                                position += 1
                            ) {
                                root.spawnCometGlyph(
                                    blockItem,
                                    markdownText,
                                    position,
                                    position - firstPosition
                                )
                            }

                            markdownText.observedLength = currentLength
                        }
                    }
                }

                Rectangle {
                    visible: blockItem.dividerBlock
                    width: parent.width
                    height: visible ? 1 : 0
                    color: root.theme.quietBorder
                    opacity: 0.9
                }

                CodeBlock {
                    id: codePanel

                    visible: blockItem.codeBlock
                    width: parent.width
                    theme: root.theme
                    code: blockItem.codeBlock
                        ? String(blockItem.modelData.content || "")
                        : ""
                    visibleCharacters: blockItem.codeBlock
                        ? blockItem.visibleCharacters
                        : 0
                    language: blockItem.codeBlock
                        ? String(blockItem.modelData.language || "")
                        : ""
                    animateReveal: root.animateReveal
                }
            }
        }
    }

    Component {
        id: cometGlyphComponent

        CometGlyph {}
    }

    Timer {
        id: revealTimer

        interval: root.theme.chatRevealFrameInterval
        repeat: true
        onTriggered: {
            const maximumSeconds = Math.max(
                1,
                root.theme.chatRevealMaximumDuration / 1000
            )
            const charactersPerSecond = Math.max(
                root.theme.chatRevealMinimumCharactersPerSecond,
                root.totalRevealLength / maximumSeconds
            )

            root.revealAccumulator += charactersPerSecond
                * interval
                / 1000

            const charactersThisFrame = Math.floor(
                root.revealAccumulator
            )

            if (charactersThisFrame < 1) {
                return
            }

            root.revealAccumulator -= charactersThisFrame

            root.revealPosition = Math.min(
                root.totalRevealLength,
                root.revealPosition + charactersThisFrame
            )

            if (root.revealPosition >= root.totalRevealLength) {
                stop()
                root.revealing = false
                root.revealProgressed()
                revealFinishTimer.start()
            }
        }
    }

    Timer {
        id: revealFinishTimer

        interval: root.theme.chatCometCoolingDuration + 80
        repeat: false
        onTriggered: root.revealFinished()
    }
}
