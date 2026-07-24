import QtQuick
import QtQuick.Controls

ScrollView {
    id: root

    required property var theme
    property string content: ""

    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    function safeMarkdown(value) {
        var safe = String(value || "")

        safe = safe.replace(
            /!\[([^\]]*)\]\(([^\n)]*)\)/g,
            function(_, alt, target) {
                var label = String(alt || "").trim()
                var source = String(target || "").trim()
                return "\n> **Image reference:** "
                    + (label.length > 0 ? label : "Untitled image")
                    + (source.length > 0 ? "  \n> `" + source + "`" : "")
                    + "\n"
            }
        )

        safe = safe.replace(
            /!\[([^\]]*)\]\s*\[[^\]]*\]/g,
            function(_, alt) {
                var label = String(alt || "").trim()
                return "\n> **Image reference:** "
                    + (label.length > 0 ? label : "Untitled image")
                    + "\n"
            }
        )

        safe = safe.replace(
            /<\s*img\b[^>]*>/gi,
            "\n> **Image reference:** Embedded HTML image\n"
        )

        return safe
    }

    function openSafeLink(link) {
        var target = String(link || "").trim()
        var lowered = target.toLowerCase()
        var allowed = lowered.indexOf("https://") === 0
            || lowered.indexOf("http://") === 0
            || lowered.indexOf("mailto:") === 0

        if (allowed) {
            Qt.openUrlExternally(target)
        }
    }

    Item {
        width: Math.max(root.availableWidth, 720)
        implicitHeight: markdownText.contentHeight + 64

        TextEdit {
            id: markdownText

            anchors.top: parent.top
            anchors.topMargin: 32
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(900, parent.width - 64)
            height: contentHeight
            text: root.safeMarkdown(root.content)
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
            font.pixelSize: root.theme.typeBody
            font.weight: Font.Normal
            font.letterSpacing: 0.08
            font.kerning: true
            font.contextFontMerging: true
            font.hintingPreference: Font.PreferNoHinting
            font.preferTypoLineMetrics: true
            renderType: TextEdit.NativeRendering
            onLinkActivated: function(link) {
                root.openSafeLink(link)
            }
        }
    }
}
