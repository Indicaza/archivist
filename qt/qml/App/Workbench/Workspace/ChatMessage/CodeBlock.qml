import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var theme
    required property string code
    required property string language
    required property int visibleCharacters
    property bool animateReveal: false

    readonly property string languageLabel: language.length > 0
        ? language.toUpperCase()
        : "CODE"
    readonly property string visibleCode: safeSlice(code, visibleCharacters)
    readonly property real viewportHeight: Math.min(
        theme.codeBlockMaxHeight,
        Math.max(theme.codeBlockMinHeight, codeText.contentHeight + 40)
    )
    property bool copied: false

    implicitHeight: codeHeader.height + viewportHeight
    height: implicitHeight

    function copyCode() {
        codeText.selectAll()
        codeText.copy()
        codeText.deselect()
        root.copied = true
        copiedReset.restart()
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

    Rectangle {
        x: 0
        y: 3
        width: parent.width
        height: parent.height
        radius: root.theme.radiusLarge
        color: "#28000000"
    }

    Rectangle {
        anchors.fill: parent
        radius: root.theme.radiusLarge
        color: root.theme.codeBlockBg
        antialiasing: true
    }

    Rectangle {
        id: codeHeader

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 42
        radius: root.theme.radiusLarge
        color: root.theme.codeBlockHeaderBg

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

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 6
                radius: 3
                color: root.theme.accent
                opacity: 0.72
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
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 62
            height: 28
            text: root.copied ? "Copied" : "Copy"
            hoverEnabled: true
            padding: 0
            onClicked: root.copyCode()

            contentItem: Text {
                text: parent.text
                color: root.copied
                    ? root.theme.success
                    : parent.hovered
                        ? root.theme.codeBlockText
                        : root.theme.codeBlockMutedText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeSize(9)
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter

                Behavior on color {
                    ColorAnimation { duration: 110 }
                }
            }

            background: Rectangle {
                radius: 7
                color: parent.hovered
                    ? root.theme.codeBlockHoverBg
                    : "transparent"
                border.width: 1
                border.color: parent.hovered
                    ? root.theme.codeBlockBorder
                    : "transparent"

                Behavior on color {
                    ColorAnimation { duration: 110 }
                }
            }
        }
    }

    Flickable {
        id: codeViewport

        anchors.top: codeHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, codeMetrics.boundingRect.width + 44)
        contentHeight: Math.max(height, codeText.contentHeight + 40)
        flickableDirection: Flickable.AutoFlickDirection
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        TextEdit {
            id: codeText

            x: 20
            y: 20
            width: Math.max(
                codeViewport.width - 40,
                codeMetrics.boundingRect.width + 4
            )
            height: Math.max(codeViewport.height - 40, contentHeight)
            text: root.visibleCode
            color: root.theme.codeBlockText
            font.family: root.theme.monospaceFontFamily
            font.pixelSize: root.theme.typeCode
            font.letterSpacing: 0.08
            font.preferTypoLineMetrics: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
            cursorVisible: false
            selectionColor: root.theme.messageSelectionBg
            selectedTextColor: root.theme.messageSelectionText
            renderType: TextEdit.NativeRendering
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
    }

    Rectangle {
        anchors.fill: parent
        radius: root.theme.radiusLarge
        color: "transparent"
        border.width: 1
        border.color: root.theme.codeBlockBorder
        antialiasing: true
        z: 10
    }

    TextMetrics {
        id: codeMetrics

        font: codeText.font
        text: root.code
    }

    Component {
        id: cometGlyphComponent

        CometGlyph {}
    }

    Timer {
        id: copiedReset

        interval: 1300
        repeat: false
        onTriggered: root.copied = false
    }
}
