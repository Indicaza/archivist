import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var theme
    required property string code
    required property string language

    readonly property string languageLabel: language.length > 0
        ? language.toUpperCase()
        : "CODE"
    readonly property real viewportHeight: Math.min(
        theme.codeBlockMaxHeight,
        Math.max(theme.codeBlockMinHeight, codeText.contentHeight + 32)
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

    Rectangle {
        anchors.fill: parent
        radius: root.theme.radiusSmall
        color: root.theme.codeBlockBg
        border.width: 1
        border.color: root.theme.codeBlockBorder
        antialiasing: true
    }

    Rectangle {
        id: codeHeader

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        radius: root.theme.radiusSmall
        color: root.theme.codeBlockHeaderBg

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.codeBlockBorder
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6
            height: 7
            radius: 3.5
                color: root.theme.accent
                opacity: 0.8
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.languageLabel
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 0.8
            }
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            width: 58
            height: 26
            text: root.copied ? "Copied" : "Copy"
            hoverEnabled: true
            padding: 0
            onClicked: root.copyCode()

            contentItem: Text {
                text: parent.text
                color: root.copied
                    ? root.theme.success
                    : parent.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: 9
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter

                Behavior on color {
                    ColorAnimation { duration: 110 }
                }
            }

            background: Rectangle {
                radius: 4
                color: parent.hovered
                    ? root.theme.hoverBg
                    : "transparent"
                border.width: 1
                border.color: parent.hovered
                    ? root.theme.panelBorder
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
        contentWidth: Math.max(width, codeMetrics.boundingRect.width + 40)
        contentHeight: Math.max(height, codeText.contentHeight + 32)
        flickableDirection: Flickable.AutoFlickDirection
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        TextEdit {
            id: codeText

            x: 18
            y: 16
            width: Math.max(
                codeViewport.width - 36,
                codeMetrics.boundingRect.width + 4
            )
            height: Math.max(codeViewport.height - 32, contentHeight)
            text: root.code
            color: root.theme.appText
            font.family: root.theme.monospaceFontFamily
            font.pixelSize: 13
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
        }
    }

    TextMetrics {
        id: codeMetrics

        font: codeText.font
        text: root.code
    }

    Timer {
        id: copiedReset

        interval: 1300
        repeat: false
        onTriggered: root.copied = false
    }
}
