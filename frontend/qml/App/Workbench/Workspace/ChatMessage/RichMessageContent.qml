import QtQuick
import Archivist.Services
import "RichMessageCache.js" as RichMessageCache

Item {
    id: root

    required property var theme
    required property string content
    property bool compact: false
    property bool transcriptMoving: false
    property bool animateReveal: false
    property int revealPosition: 0
    property real revealAccumulator: 0
    property bool revealing: false
    property bool revealInitialized: false
    property string cacheKey: ""
    // True while syncBlockModel() rewrites the blocks. A rebind changes a
    // block's kind, content and language one at a time; the block texts
    // collect them and lay out once, synchronously, when it finishes.
    property bool syncing: false
    // The speaking agent's hue, and the reading styles built from it.
    property color hue: theme.accent
    readonly property var readingStyle: theme.chatReadingStyle(hue)
    // Prose stops at a comfortable measure; code and tables use the width.
    readonly property real textMeasure: compact
        ? width
        : Math.min(width, theme.chatReadingMeasure)

    readonly property var blocks: cachedBlocks(cacheKey, content)
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

    Component.onCompleted: {
        root.syncBlockModel()
        revealSetupTimer.start()
    }

    onBlocksChanged: root.syncBlockModel()

    // Streaming replaces `blocks` every flush. Copying it into a ListModel row by
    // row keeps finished blocks' text items alive, so only the block that is
    // still growing re-lays out its markdown.
    function normalizedBlock(block) {
        return {
            content: String(block.content || ""),
            language: String(block.language || ""),
            fileName: String(block.fileName || ""),
            complete: block.complete !== false,
            revealStart: Number(block.revealStart || 0),
            revealEnd: Number(block.revealEnd || 0),
            kind: String(block.kind || "markdown")
        }
    }

    function syncBlockModel() {
        root.syncing = true
        try {
            root.applyBlocks(root.blocks || [])
        } finally {
            root.syncing = false
        }
    }

    function applyBlocks(nextBlocks) {
        for (let index = 0; index < nextBlocks.length; index += 1) {
            const next = root.normalizedBlock(nextBlocks[index])

            if (index >= blockModel.count) {
                blockModel.append(next)
                continue
            }

            const current = blockModel.get(index)

            // A recycled row can turn a text block into a code block (or
            // back). Empty the content first so neither side lays out the
            // other's text on the way through.
            if (current.kind !== next.kind) {
                if (current.content.length > 0) {
                    blockModel.setProperty(index, "content", "")
                }

                blockModel.setProperty(index, "kind", next.kind)
            }

            for (const key in next) {
                if (current[key] !== next[key]) {
                    blockModel.setProperty(index, key, next[key])
                }
            }
        }

        if (blockModel.count > nextBlocks.length) {
            blockModel.remove(
                nextBlocks.length,
                blockModel.count - nextBlocks.length
            )
        }
    }

    ListModel {
        id: blockModel
    }

    onAnimateRevealChanged: {
        if (revealInitialized) {
            configureReveal()
        }
    }

    onContentChanged: {
        if (!revealInitialized) {
            return
        }

        if (animateReveal) {
            configureReveal()
            return
        }

        revealTimer.stop()
        revealFinishTimer.stop()
        revealPosition = totalRevealLength
        revealing = false
    }

    // Block splitting runs in C++ (MessageBlockParser) so streaming flushes
    // do not re-run a regex-heavy JavaScript parse over the whole reply.
    function parseBlocks(value) {
        return MessageBlocks.parse(String(value))
    }

    function cachedBlocks(key, value) {
        const stableKey = String(key || "")

        if (stableKey.length > 0) {
            const cached = RichMessageCache.getBlocks(
                stableKey,
                value
            )

            if (cached !== null) {
                return cached
            }
        }

        const parsed = parseBlocks(value)

        if (stableKey.length > 0) {
            RichMessageCache.putBlocks(
                stableKey,
                value,
                parsed
            )
        }

        return parsed
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

    // Stacks the blocks. Not a Column: a Column lays out at polish time,
    // after the ListView has placed the row, so a recycled row would resize
    // after placement and push the rows below it. Plain bindings keep the
    // height exact as soon as the content changes.
    Item {
        id: contentColumn

        readonly property real spacing: root.compact ? 16 : 24
        // Bumped when blocks are added or removed, so the stacking bindings
        // look the neighbours up again.
        property int revision: 0

        // The y for whatever follows `item`, which sits at `y`. Hidden
        // blocks take no space, as in a Column.
        function below(item, y) {
            return item.visible ? y + item.height + spacing : y
        }

        width: parent.width
        implicitHeight: {
            contentColumn.revision

            for (let index = blockRepeater.count - 1; index >= 0; index -= 1) {
                const item = blockRepeater.itemAt(index)

                if (item) {
                    return Math.max(0, contentColumn.below(item, item.y) - spacing)
                }
            }

            return 0
        }

        Repeater {
            id: blockRepeater

            model: blockModel
            onItemAdded: contentColumn.revision += 1
            onItemRemoved: contentColumn.revision += 1

            delegate: Item {
                id: blockItem

                required property int index
                required property string kind
                required property string content
                required property string language
                required property string fileName
                required property bool complete
                required property real revealStart
                required property real revealEnd

                readonly property var modelData: ({
                    kind: blockItem.kind,
                    content: blockItem.content,
                    language: blockItem.language,
                    fileName: blockItem.fileName,
                    complete: blockItem.complete,
                    revealStart: blockItem.revealStart,
                    revealEnd: blockItem.revealEnd
                })

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

                y: {
                    contentColumn.revision
                    const previous = index > 0 ? blockRepeater.itemAt(index - 1) : null

                    return previous ? contentColumn.below(previous, previous.y) : 0
                }
                width: contentColumn.width
                visible: revealed
                implicitHeight: !visible
                    ? 0
                    : codeBlock
                    ? (codeLoader.item ? codeLoader.item.implicitHeight : 0)
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

                ChatText {
                    id: markdownText

                    visible: !blockItem.codeBlock && !blockItem.dividerBlock
                    // Tables keep the full width; prose wraps at the measure.
                    width: /^\s*\|?\s*:?-{2,}:?\s*\|/m.test(blockItem.content)
                        ? parent.width
                        : root.textMeasure
                    height: visible ? contentHeight : 0
                    // Text that is still being revealed changes every frame,
                    // so it skips the layout cache.
                    cacheable: !root.animateReveal
                    layoutSuspended: root.syncing
                    markdown: true
                    wrap: true
                    text: visible
                        ? root.safeSlice(
                            String(blockItem.modelData.content || ""),
                            blockItem.visibleCharacters
                        )
                        : ""
                    color: root.theme.appText
                    linkColor: root.theme.chatLinkColor(root.hue)
                    font: root.theme.chatBodyFont
                    styling: root.readingStyle
                    selectionColor: root.theme.messageSelectionBg
                    selectedTextColor: root.theme.messageSelectionText
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

                // Only code blocks build the (heavy) code panel.
                Loader {
                    id: codeLoader

                    active: blockItem.codeBlock
                    // Code lines up with the prose and may run a little
                    // past it, so the text edges stay put.
                    width: Math.min(parent.width, root.textMeasure + root.theme.chatCodeOverhang)
                    sourceComponent: Component {
                        CodeBlock {
                            width: codeLoader.width
                            theme: root.theme
                            code: String(blockItem.modelData.content || "")
                            visibleCharacters: blockItem.visibleCharacters
                            language: String(blockItem.modelData.language || "")
                            fileName: String(blockItem.modelData.fileName || "")
                            complete: blockItem.complete
                            foldKey: root.cacheKey + "#" + String(blockItem.revealStart)
                            hue: root.hue
                            animateReveal: root.animateReveal
                            layoutSuspended: root.syncing
                            transcriptMoving: root.transcriptMoving
                        }
                    }
                }
            }
        }
    }

    Component {
        id: cometGlyphComponent

        CometGlyph {}
    }

    Timer {
        id: revealSetupTimer

        interval: 0
        repeat: false
        onTriggered: root.configureReveal()
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
