import QtQuick
import QtQuick.Controls
import Archivist.Services
import "CodeFoldState.js" as CodeFoldState

Item {
    id: root

    required property var theme
    required property string code
    required property string language
    required property int visibleCharacters
    property string fileName: ""
    property bool complete: true
    // Identifies this block across row recycling, for its fold state.
    property string foldKey: ""
    property color hue: theme.accent
    property bool animateReveal: false
    property bool layoutSuspended: false
    // True while the transcript scrolls. Scrolling the page never activates
    // a code block, even when the pointer ends up resting on one.
    property bool transcriptMoving: false

    // A code block lets the page scroll straight past it until the pointer
    // rests on it for a moment; then it comes into focus (lifts, grows a
    // little, its edge lights up, line numbers come up) and only then takes
    // scroll input.
    property bool engaged: false
    readonly property bool warming: hover.hovered
        && !root.engaged
        && !root.transcriptMoving
    readonly property bool scrollable: codeViewport.contentHeight > codeViewport.height + 1
        || codeViewport.contentWidth > codeViewport.width + 1
    // 0 at rest, 1 in focus; sprung, so it overshoots a touch on arrival.
    property real focusAmount: engaged ? 1 : 0
    readonly property real grow: theme.codeBlockFocusGrow * focusAmount
    readonly property color edgeColor: Qt.tint(
        theme.codeBlockBorder,
        Qt.rgba(
            hue.r,
            hue.g,
            hue.b,
            Math.max(0, Math.min(1, 0.75 * focusAmount + (warming ? 0.18 : 0)))
        )
    )

    readonly property int headerHeight: 42
    readonly property int codePadding: 16
    readonly property int totalLines: code.length === 0
        ? 0
        : code.split("\n").length
    readonly property int digits: Math.max(2, String(Math.max(1, totalLines)).length)
    readonly property real gutterWidth: digits * codeMetrics.advanceWidth("0") + 26
    readonly property real lineHeight: codeText.lineCount > 0
        ? codeText.contentHeight / codeText.lineCount
        : codeMetrics.height
    // Long blocks show their first lines and fold the rest away.
    readonly property bool foldable: totalLines > theme.codeBlockFoldLines + 2
    property bool expanded: CodeFoldState.isExpanded(foldKey)
    readonly property bool folded: foldable && !expanded
    readonly property int hiddenLines: Math.max(0, totalLines - theme.codeBlockFoldLines)
    readonly property real foldedCodeHeight: theme.codeBlockFoldLines * lineHeight
    readonly property real footerHeight: foldable ? 34 : 0

    readonly property string languageLabel: language.length > 0
        ? language.toUpperCase()
        : "CODE"
    readonly property string lineLabel: totalLines === 1
        ? "1 line"
        : totalLines + " lines"
    readonly property string visibleCode: safeSlice(code, visibleCharacters)
    readonly property real viewportHeight: folded
        ? foldedCodeHeight + 2 * codePadding
        : Math.min(
            theme.codeBlockMaxHeight,
            Math.max(theme.codeBlockMinHeight, codeText.contentHeight + 2 * codePadding)
        )
    property bool copied: false

    implicitHeight: headerHeight + viewportHeight + footerHeight
    height: implicitHeight

    // A reply that just arrived keeps its code open: folding it after it
    // was written in front of you would shrink the row under the reader.
    // Only history loads folded. The choice is remembered, so a recycled
    // row comes back the same height.
    function keepFreshCodeOpen() {
        if (!animateReveal || foldKey.length === 0 || expanded) {
            return
        }

        CodeFoldState.setExpanded(foldKey, true)
        expanded = true
    }

    onFoldKeyChanged: {
        expanded = CodeFoldState.isExpanded(foldKey)
        keepFreshCodeOpen()
    }
    onAnimateRevealChanged: keepFreshCodeOpen()
    Component.onCompleted: keepFreshCodeOpen()

    Behavior on focusAmount {
        enabled: !root.theme.reducedMotion

        SpringAnimation {
            spring: root.theme.codeBlockFocusSpring
            damping: root.theme.codeBlockFocusDamping
            epsilon: 0.002
        }
    }

    onTranscriptMovingChanged: {
        if (transcriptMoving) {
            focusDelay.stop()
            engaged = false
        } else if (hover.hovered) {
            focusDelay.restart()
        }
    }

    function toggleFold() {
        root.expanded = !root.expanded
        CodeFoldState.setExpanded(root.foldKey, root.expanded)

        if (!root.expanded) {
            codeViewport.contentY = 0
        }
    }

    function copyCode() {
        ChatUi.copyText(root.code)
        root.copied = true
        copiedReset.restart()

        if (!root.theme.reducedMotion) {
            copyPop.restart()
            copySweep.restart()
        }
    }

    function lineNumbers(count) {
        const lines = []

        for (let line = 1; line <= count; line += 1) {
            lines.push(String(line).padStart(root.digits, " "))
        }

        return lines.join("\n")
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

    function spawnCometGlyph(position, order) {
        const firstUnit = codeText.getText(position, position + 1)
        const firstCode = firstUnit.length > 0
            ? firstUnit.charCodeAt(0)
            : 0

        if (
            firstCode >= 0xDC00
            && firstCode <= 0xDFFF
            && position > 0
        ) {
            const previousUnit = codeText.getText(position - 1, position)
            const previousCode = previousUnit.length > 0
                ? previousUnit.charCodeAt(0)
                : 0

            if (previousCode >= 0xD800 && previousCode <= 0xDBFF) {
                return
            }
        }

        const glyph = firstCode >= 0xD800 && firstCode <= 0xDBFF
            ? codeText.getText(position, position + 2)
            : firstUnit

        if (glyph.trim().length === 0) {
            return
        }

        const glyphRect = codeText.positionToRectangle(position)
        const glyphSize = codeText.font.pixelSize
        const visualY = glyphRect.y + Math.max(
            0,
            (glyphRect.height - glyphSize * 1.24) / 2
        )

        cometGlyphComponent.createObject(codeText, {
            glyph: glyph,
            glyphFontFamily: codeText.font.family,
            glyphFontSize: glyphSize,
            hotColor: theme.cometHotText,
            warmColor: theme.cometWarmText,
            settledColor: theme.codeBlockText,
            settledX: glyphRect.x,
            settledY: visualY,
            rise: theme.chatCometRise,
            startScale: theme.chatCometStartScale,
            settleDuration: theme.chatCometSettleDuration,
            coolingDuration: theme.chatCometCoolingDuration,
            birthDelay: Math.min(56, order * 8)
        })
    }

    FontMetrics {
        id: codeMetrics

        font: root.theme.chatCodeFont
    }

    HoverHandler {
        id: hover

        onHoveredChanged: {
            if (hovered && !root.transcriptMoving) {
                focusDelay.restart()
            } else if (!hovered) {
                focusDelay.stop()
                root.engaged = false
            }
        }
    }

    Timer {
        id: focusDelay

        interval: root.theme.codeBlockFocusDelay
        repeat: false
        onTriggered: root.engaged = hover.hovered && !root.transcriptMoving
    }

    // Glow that blooms around the block when it comes into focus.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.grow - 5
        radius: root.theme.radiusLarge + root.grow + 5
        color: "transparent"
        border.width: 5
        border.color: root.theme.withAlpha(root.hue, 0.15)
        opacity: Math.max(0, Math.min(1, root.focusAmount))
        visible: opacity > 0.01
        antialiasing: true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.grow
        anchors.topMargin: -root.grow + 3 + 6 * root.focusAmount
        anchors.bottomMargin: -root.grow - 3 - 6 * root.focusAmount
        radius: root.theme.radiusLarge + root.grow
        color: Qt.rgba(0, 0, 0, 0.16 + 0.14 * Math.max(0, root.focusAmount))
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.grow
        radius: root.theme.radiusLarge + root.grow
        color: Qt.tint(
            root.theme.codeBlockBg,
            Qt.rgba(1, 1, 1, 0.025 * Math.max(0, root.focusAmount))
        )
        antialiasing: true
    }

    Rectangle {
        id: codeHeader

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: -root.grow
        anchors.leftMargin: -root.grow
        anchors.rightMargin: -root.grow
        height: root.headerHeight + root.grow
        radius: root.theme.radiusLarge + root.grow
        color: root.theme.codeBlockHeaderBg
        clip: true

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.theme.radiusLarge
            color: parent.color
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.codeBlockBorder
        }

        // A light sweep across the header when the code is copied.
        Rectangle {
            id: sweep

            y: 0
            width: 120
            height: parent.height
            x: -width
            opacity: 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: root.theme.withAlpha(root.theme.success, 0.16) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: copyButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            clip: true

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 6
                radius: 3
                color: root.engaged
                    ? root.theme.agentHueBright(root.hue)
                    : root.hue
                opacity: 0.72 + 0.28 * Math.max(0, Math.min(1, root.focusAmount))
                // The dot pops as the block activates.
                scale: 1 + 0.7 * Math.max(0, root.focusAmount - 0.6)
            }

            LanguageIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.language.length > 0
                languageId: root.language
                fileName: root.fileName
                iconSize: 14
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.languageLabel
                color: root.theme.codeBlockMutedText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeSize(9)
                font.weight: Font.DemiBold
                font.letterSpacing: 0.7
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.fileName.length > 0
                text: root.fileName
                color: root.theme.codeBlockText
                font.family: root.theme.monospaceFontFamily
                font.pixelSize: root.theme.typeSize(10)
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, 320)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.totalLines > 1
                text: "·  " + root.lineLabel
                color: root.theme.codeBlockMutedText
                opacity: 0.75
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeSize(9)
            }
        }

        Item {
            id: copyButton

            readonly property bool hovered: copyMouse.containsMouse

            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 74
            height: 28
            Accessible.role: Accessible.Button
            Accessible.name: root.copied ? "Copied" : "Copy code"

            Rectangle {
                anchors.fill: parent
                radius: 7
                color: copyButton.hovered
                    ? root.theme.codeBlockHoverBg
                    : "transparent"
                border.width: 1
                border.color: copyButton.hovered
                    ? root.theme.codeBlockBorder
                    : "transparent"

                Behavior on color {
                    ColorAnimation { duration: 110 }
                }
            }

            Text {
                id: copyLabel

                anchors.centerIn: parent
                text: root.copied ? "✓  Copied" : "Copy"
                color: root.copied
                    ? root.theme.success
                    : copyButton.hovered
                        ? root.theme.codeBlockText
                        : root.theme.codeBlockMutedText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeSize(9)
                font.weight: Font.DemiBold

                Behavior on color {
                    ColorAnimation { duration: 110 }
                }
            }

            MouseArea {
                id: copyMouse

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.copyCode()
            }
        }
    }

    Flickable {
        id: codeViewport

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 1
        anchors.topMargin: root.headerHeight
        height: root.viewportHeight - 1
        clip: true
        // Until the block is in focus, wheel and trackpad input passes
        // through to the transcript.
        interactive: root.engaged && root.scrollable
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, codeText.x + codeText.contentWidth + 24)
        contentHeight: root.folded
            ? height
            : Math.max(height, codeText.contentHeight + 2 * root.codePadding)
        flickableDirection: Flickable.AutoFlickDirection
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
            opacity: root.engaged ? 1 : 0.35
        }
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            opacity: root.engaged ? 1 : 0.35
        }

        ChatText {
            id: codeText

            x: root.gutterWidth + 12
            y: root.codePadding
            width: Math.max(codeViewport.width - x - 20, contentWidth + 4)
            height: Math.max(codeViewport.height - 2 * root.codePadding, contentHeight)
            cacheable: !root.animateReveal
            layoutSuspended: root.layoutSuspended
            markdown: false
            wrap: false
            language: root.language
            styling: root.theme.chatSyntaxStyle
            // Diff tints run under the gutter and out to the edges.
            decorationBleed: x
            text: root.visibleCode
            color: root.theme.codeBlockText
            font: root.theme.chatCodeFont
            selectionColor: root.theme.messageSelectionBg
            selectedTextColor: root.theme.messageSelectionText
            property int observedLength: 0

            onTextChanged: glyphCapture.restart()

            Component.onCompleted: {
                observedLength = root.animateReveal ? 0 : length

                if (root.animateReveal && length > 0) {
                    glyphCapture.restart()
                }
            }

            Timer {
                id: glyphCapture

                interval: 0
                repeat: false
                onTriggered: {
                    const currentLength = codeText.length

                    if (!root.animateReveal) {
                        codeText.observedLength = currentLength
                        return
                    }

                    if (currentLength <= codeText.observedLength) {
                        codeText.observedLength = currentLength
                        return
                    }

                    const firstPosition = Math.max(
                        codeText.observedLength,
                        currentLength
                            - root.theme.chatCometMaximumGlyphsPerFrame
                    )

                    for (
                        let position = firstPosition;
                        position < currentLength;
                        position += 1
                    ) {
                        root.spawnCometGlyph(
                            position,
                            position - firstPosition
                        )
                    }

                    codeText.observedLength = currentLength
                }
            }
        }

        // Line numbers stay put while the code scrolls sideways, faint at
        // rest and clear once the block is in focus.
        Rectangle {
            x: codeViewport.contentX
            y: 0
            width: root.gutterWidth
            height: codeViewport.contentHeight
            color: root.theme.codeBlockBg

            ChatText {
                x: 12
                y: root.codePadding
                width: root.gutterWidth - 18
                markdown: false
                wrap: false
                selectable: false
                layoutSuspended: root.layoutSuspended
                text: root.lineNumbers(Math.max(1, codeText.lineCount))
                color: root.theme.codeGutterText
                font: root.theme.chatCodeFont
                opacity: 0.4 + 0.6 * Math.max(0, Math.min(1, root.focusAmount))
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: root.theme.quietBorder
                opacity: 0.6
            }
        }
    }

    // Folded blocks fade out into the expand bar.
    Rectangle {
        anchors.left: codeViewport.left
        anchors.right: codeViewport.right
        anchors.bottom: codeViewport.bottom
        height: 56
        visible: root.folded
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.theme.withAlpha(root.theme.codeBlockBg, 0) }
            GradientStop { position: 1.0; color: root.theme.codeBlockBg }
        }
    }

    Item {
        id: foldBar

        readonly property bool hovered: foldMouse.containsMouse

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.footerHeight
        visible: root.foldable
        Accessible.role: Accessible.Button
        Accessible.name: foldLabel.text

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.top: parent.top
            height: 1
            color: root.theme.codeBlockBorder
            opacity: 0.6
        }

        Text {
            id: foldLabel

            anchors.centerIn: parent
            text: root.folded
                ? "Expand " + root.hiddenLines + " more lines  ⌄"
                : "Show less  ⌃"
            color: foldBar.hovered
                ? root.theme.agentHueBright(root.hue)
                : root.theme.codeBlockMutedText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.DemiBold

            Behavior on color {
                ColorAnimation { duration: 110 }
            }
        }

        MouseArea {
            id: foldMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleFold()
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.grow
        radius: root.theme.radiusLarge + root.grow
        color: "transparent"
        border.width: 1
        border.color: root.edgeColor
        antialiasing: true
        z: 10

        Behavior on border.color {
            enabled: !root.engaged
            ColorAnimation { duration: root.theme.motionHover }
        }
    }

    SequentialAnimation {
        id: copyPop

        NumberAnimation {
            target: copyLabel
            property: "scale"
            to: 1.18
            duration: 90
            easing.type: Easing.OutCubic
        }

        SpringAnimation {
            target: copyLabel
            property: "scale"
            to: 1
            spring: 4.5
            damping: 0.25
            epsilon: 0.002
        }
    }

    ParallelAnimation {
        id: copySweep

        NumberAnimation {
            target: sweep
            property: "x"
            from: -sweep.width
            to: codeHeader.width
            duration: 520
            easing.type: Easing.InOutQuad
        }

        SequentialAnimation {
            NumberAnimation { target: sweep; property: "opacity"; to: 1; duration: 120 }
            PauseAnimation { duration: 280 }
            NumberAnimation { target: sweep; property: "opacity"; to: 0; duration: 120 }
        }
    }

    Component {
        id: cometGlyphComponent

        CometGlyph {}
    }

    Timer {
        id: copiedReset

        interval: 1400
        repeat: false
        onTriggered: root.copied = false
    }
}
