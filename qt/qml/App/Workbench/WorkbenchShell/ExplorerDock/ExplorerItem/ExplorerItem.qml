import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var theme
    required property var dragSession
    required property Item dragProxy
    required property bool dragEnabled
    required property string title
    required property string glyph
    required property int depth
    required property bool selected
    required property bool muted
    required property bool folder
    required property bool expanded
    required property bool warning
    required property bool neighborHovered
    required property string fileId
    required property string relativePath

    property bool dropHighlighted: false
    readonly property bool hovered: rowHover.hovered
    readonly property bool dragging: fileDrag.active
    readonly property string sourceDirectory: directoryForPath(relativePath)

    signal activated()
    signal fileDropRequested(string fileId, string targetDirectory)

    width: parent ? parent.width : 220
    height: 27
    opacity: dragging ? 0.38 : 1.0

    function directoryForPath(filePath) {
        var normalized = String(filePath || "").split("\\").join("/")
        var separator = normalized.lastIndexOf("/")
        return separator >= 0 ? normalized.slice(0, separator) : ""
    }

    function canAcceptDrop() {
        return root.folder
            && root.dragEnabled
            && root.dragSession.active
            && root.dragSession.payloadType === "library-file"
            && String(root.dragSession.payload.sourceDirectory || "")
                !== root.relativePath
    }

    DragHandler {
        id: fileDrag

        enabled: root.dragEnabled
            && !root.folder
            && !root.muted
            && root.fileId.length > 0
        target: root.dragProxy

        onActiveChanged: {
            if (active) {
                var mapped = root.mapToItem(root.dragProxy.parent, 0, 0)
                root.dragProxy.x = mapped.x
                root.dragProxy.y = mapped.y
                root.dragSession.begin(
                    "library-file",
                    {
                        id: root.fileId,
                        relativePath: root.relativePath,
                        sourceDirectory: root.sourceDirectory,
                        title: root.title,
                        glyph: root.glyph
                    },
                    root.title
                )
                return
            }

            if (
                root.dragSession.active
                && root.dragSession.payloadType === "library-file"
                && String(root.dragSession.payload.id || "") === root.fileId
            ) {
                var action = root.dragProxy.Drag.drop()
                root.dragSession.finish(action === Qt.MoveAction)
            }

            root.dragProxy.x = 0
            root.dragProxy.y = 0
        }
    }

    HoverHandler {
        id: rowHover
    }

    TapHandler {
        enabled: !root.dragging
        onTapped: root.activated()
    }

    DropArea {
        id: folderDropArea

        anchors.fill: parent
        keys: ["archivist-library-file"]
        enabled: root.folder && root.dragEnabled

        onEntered: function(drag) {
            var allowed = root.canAcceptDrop()
            drag.accepted = allowed
            root.dropHighlighted = allowed
            root.dragSession.setTarget(
                "library-folder",
                root.relativePath,
                root.title,
                allowed
            )
        }

        onPositionChanged: function(drag) {
            drag.accepted = root.canAcceptDrop()
        }

        onExited: {
            root.dropHighlighted = false
            root.dragSession.clearTarget(root.relativePath)
        }

        onDropped: function(drop) {
            var allowed = root.canAcceptDrop()
            root.dropHighlighted = false

            if (!allowed) {
                return
            }

            root.fileDropRequested(
                String(root.dragSession.payload.id || ""),
                root.relativePath
            )
            drop.acceptProposedAction()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.dropHighlighted
            ? root.theme.activeBg
            : root.selected
                ? root.theme.activeBg
                : root.hovered
                    ? root.theme.hoverBg
                    : "transparent"
        border.width: root.dropHighlighted ? 1 : 0
        border.color: root.theme.accentBright

        Behavior on color {
            ColorAnimation { duration: root.theme.motionFast }
        }
    }

    Item {
        anchors.fill: parent
        x: root.hovered ? 3 : root.neighborHovered ? 1.5 : 0

        Behavior on x {
            NumberAnimation {
                duration: root.hovered || root.neighborHovered
                    ? root.theme.motionHover
                    : root.theme.motionHoverExit
                easing.type: root.hovered || root.neighborHovered
                    ? Easing.OutBack
                    : Easing.OutCubic
            }
        }

        Text {
            x: 7 + root.depth * 13
            width: 10
            height: parent.height
            visible: root.folder
            text: root.expanded ? "⌄" : "›"
            color: root.hovered || root.dropHighlighted
                ? root.theme.appText
                : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(13)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 19 + root.depth * 13
            width: 15
            height: parent.height
            text: root.glyph
            color: root.dropHighlighted
                ? root.theme.accentBright
                : root.muted
                    ? "#756e63"
                    : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(root.folder ? 14 : 13)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            x: 38 + root.depth * 13
            width: Math.max(0, parent.width - x - (root.warning ? 22 : 5))
            height: parent.height
            text: root.title
            color: root.muted ? "#756e63" : root.theme.appText
            font.pixelSize: root.theme.typeSize(11)
            font.strikeout: root.muted
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            visible: root.warning
            text: "△"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(11)
        }
    }
}
