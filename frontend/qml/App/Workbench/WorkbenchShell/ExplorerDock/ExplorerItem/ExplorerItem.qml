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
    required property bool active
    required property bool muted
    required property bool folder
    required property bool expanded
    required property bool warning
    required property bool neighborHovered
    required property string fileId
    required property string relativePath
    required property string gitStatus
    required property int gitCount

    property bool dropHighlighted: false
    readonly property bool hovered: rowHover.hovered
    readonly property bool dragging: fileDrag.active
    readonly property string sourceDirectory: directoryForPath(relativePath)
    readonly property color statusColor: gitColor(gitStatus)
    readonly property string statusText: gitLabel(gitStatus)
    readonly property bool showsStatus: gitStatus.length > 0
        || (folder && gitCount > 0)

    signal activated()
    signal contextRequested()
    signal fileDropRequested(string fileId, string targetDirectory)

    width: parent ? parent.width : 220
    height: 26
    opacity: dragging ? 0.38 : 1.0

    function directoryForPath(filePath) {
        var normalized = String(filePath || "").split("\\").join("/")
        var separator = normalized.lastIndexOf("/")
        return separator >= 0 ? normalized.slice(0, separator) : ""
    }

    function gitColor(status) {
        switch (String(status || "")) {
        case "modified": return "#d7a84f"
        case "added": return "#74b886"
        case "untracked": return "#68a7d3"
        case "deleted": return "#df7479"
        case "renamed": return "#b79ad6"
        case "conflicted": return "#f06f75"
        default: return root.theme.mutedText
        }
    }

    function gitLabel(status) {
        switch (String(status || "")) {
        case "modified": return "M"
        case "added": return "A"
        case "untracked": return "?"
        case "deleted": return "D"
        case "renamed": return "R"
        case "conflicted": return "U"
        default: return ""
        }
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
        acceptedButtons: Qt.LeftButton
        enabled: !root.dragging
        onTapped: root.activated()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: !root.dragging
        onTapped: root.contextRequested()
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
        radius: 3
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

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 1
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: 17
        radius: 1
        visible: root.active && !root.folder
        color: root.theme.accentBright
    }

    Item {
        anchors.fill: parent
        x: root.hovered ? 2 : root.neighborHovered ? 1 : 0

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

        Repeater {
            model: Math.max(0, root.depth)

            delegate: Rectangle {
                required property int index

                x: 13 + index * 14
                y: 0
                width: 1
                height: parent.height
                color: root.theme.quietBorder
                opacity: 0.48
            }
        }

        Text {
            x: 6 + root.depth * 14
            width: 12
            height: parent.height
            visible: root.folder
            text: root.expanded ? "⌄" : "›"
            color: root.hovered || root.dropHighlighted
                ? root.theme.appText
                : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(12)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            x: 20 + root.depth * 14
            anchors.verticalCenter: parent.verticalCenter
            width: 19
            height: 17
            radius: 3
            color: root.folder
                ? root.theme.controlSurfaceBg
                : root.showsStatus
                    ? Qt.rgba(
                        root.statusColor.r,
                        root.statusColor.g,
                        root.statusColor.b,
                        0.12
                    )
                    : root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.showsStatus
                ? Qt.rgba(
                    root.statusColor.r,
                    root.statusColor.g,
                    root.statusColor.b,
                    0.34
                )
                : root.theme.quietBorder

            Text {
                anchors.centerIn: parent
                text: root.glyph
                color: root.showsStatus
                    ? root.statusColor
                    : root.muted
                        ? "#756e63"
                        : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(
                    root.glyph.length > 1 ? 7 : 9
                )
                font.weight: Font.DemiBold
            }
        }

        Text {
            id: titleText

            x: 44 + root.depth * 14
            width: Math.max(
                0,
                parent.width - x - (
                    root.showsStatus ? 31 : root.warning ? 24 : 7
                )
            )
            height: parent.height
            text: root.title
            color: root.showsStatus
                ? root.statusColor
                : root.muted
                    ? "#756e63"
                    : root.active
                        ? root.theme.appText
                        : root.folder
                            ? root.theme.appText
                            : root.theme.appText
            font.pixelSize: root.theme.typeSize(11)
            font.weight: root.folder || root.active
                ? Font.DemiBold
                : Font.Normal
            font.strikeout: root.muted || root.gitStatus === "deleted"
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showsStatus
            text: root.folder
                ? String(root.gitCount)
                : root.statusText
            color: root.statusColor
            font.pixelSize: root.theme.typeSize(root.folder ? 9 : 10)
            font.weight: Font.DemiBold
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            visible: root.warning && !root.showsStatus
            text: "△"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(10)
        }
    }
}
