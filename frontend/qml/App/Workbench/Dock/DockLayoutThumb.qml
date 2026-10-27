import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// A miniature of a layout: one tile per panel, tinted by what it is for.
Item {
    id: root

    required property var theme
    // [{x, y, width, height, kind}] in 0..1, from Docking.layoutPreview().
    property var rects: []
    property bool active: false
    property bool hovered: false

    function tint(kind) {
        switch (kind) {
        case "editor": return "#9280bc"
        case "chat": return "#5fae9f"
        case "terminal": return "#b8a06a"
        case "preview": return "#6f97c9"
        case "diff": return "#7fae7a"
        default: return "#7a756b"
        }
    }

    function iconFor(kind) {
        switch (kind) {
        case "editor": return "file-text"
        case "chat": return "chat"
        case "terminal": return "terminal"
        case "preview": return "file-document"
        case "diff": return "git-branch"
        default: return "grid"
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: root.theme.workspaceBgDeep
        border.width: root.active ? 1.5 : 1
        border.color: root.active ? root.theme.accentBright : root.hovered ? root.theme.panelBorder : root.theme.quietBorder

        Behavior on border.color { ColorAnimation { duration: root.theme.motionFast } }
    }

    Item {
        id: canvas

        anchors.fill: parent
        anchors.margins: 5

        Repeater {
            model: root.rects

            delegate: Rectangle {
                required property var modelData
                required property int index

                readonly property color base: root.tint(String(modelData.kind || "any"))

                x: Number(modelData.x) * canvas.width + 1
                y: Number(modelData.y) * canvas.height + 1
                width: Math.max(2, Number(modelData.width) * canvas.width - 2)
                height: Math.max(2, Number(modelData.height) * canvas.height - 2)
                radius: 3
                color: Qt.rgba(base.r, base.g, base.b, root.hovered || root.active ? 0.34 : 0.24)
                border.width: 1
                border.color: Qt.rgba(base.r, base.g, base.b, 0.55)

                Behavior on color { ColorAnimation { duration: root.theme.motionFast } }

                // A little header strip, like the real panels.
                Rectangle {
                    x: 1
                    y: 1
                    width: parent.width - 2
                    height: Math.min(4, parent.height / 4)
                    radius: 2
                    color: Qt.rgba(parent.base.r, parent.base.g, parent.base.b, 0.5)
                }

                AppIcon {
                    anchors.centerIn: parent
                    visible: parent.width > 22 && parent.height > 18
                    name: root.iconFor(String(modelData.kind || "any"))
                    tone: "muted"
                    iconSize: Math.min(14, Math.max(9, Math.min(parent.width, parent.height) * 0.32))
                    opacity: 0.85
                }
            }
        }
    }
}
