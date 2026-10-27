import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"
import "../../Files/FileIdentity.js" as FileIdentity

// One tab: one view. Click activates, middle-click closes, dragging moves
// just this view (to another tab position, another Anchor, or an edge).
Item {
    id: root

    required property var theme
    required property var workspace
    required property string anchorId
    required property string viewId
    required property int tabIndex
    required property bool active
    required property bool anchorFocused

    readonly property var info: {
        Docking.revision
        return Docking.view(root.viewId)
    }
    readonly property var resource: info.resource || ({})
    readonly property var details: resource.details || ({})
    readonly property string typeId: String(info.typeId || "")
    readonly property string documentId: typeId === "archivist.editor" ? String(resource.id || "") : ""
    readonly property bool dirty: {
        Documents.revision
        return documentId.length > 0 && Documents.isDirty(documentId)
    }
    readonly property string title: root.workspace.viewTitle(info)
    readonly property string worktreeLabel: root.workspace.worktreeLabel(resource)
    readonly property var fileIdentity: FileIdentity.resolve({
        fileName: String(details.name || details.path || root.title),
        extension: String(details.extension || "")
    })
    // Files that read differently rendered than as source.
    readonly property bool hasRenderedForm: ["markdown", "structured-json", "structured-yaml", "svg"]
        .indexOf(String(fileIdentity.preferredRendererId || "")) >= 0
    readonly property string iconName: {
        switch (typeId) {
        case "archivist.chat":
            return "chat"
        case "archivist.terminal":
            return "terminal"
        case "archivist.diff":
            return "git-branch"
        case "archivist.welcome":
            return "workspace"
        default:
            break
        }
        var identity = root.fileIdentity
        return identity.category === "image"
            ? "file-image"
            : identity.preferredRendererId === "markdown"
                ? "file-markdown"
                : identity.preferredRendererId === "pdf"
                    ? "file-pdf"
                    : identity.category === "code" || identity.category === "data"
                        ? "file-text"
                        : "file"
    }

    // The tab being dragged stays behind as a faded outline of where it was.
    readonly property bool lifted: Docking.dragActive && Docking.dragKind === "view" && Docking.dragId === root.viewId
    opacity: lifted ? 0.4 : 1
    Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }

    width: Math.min(220, Math.max(96, label.implicitWidth + (worktreeText.visible ? worktreeText.implicitWidth + 6 : 0) + 58))

    Rectangle {
        anchors.fill: parent
        color: root.active
            ? root.theme.workspaceBg
            : tabArea.containsMouse ? root.theme.hoverBg : "transparent"

        Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        visible: root.active
        color: root.anchorFocused ? root.theme.accent : root.theme.panelBorder
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: root.theme.quietBorder
    }

    AppIcon {
        id: icon

        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        name: root.iconName
        tone: root.active ? "normal" : "muted"
        iconSize: 13
    }

    Text {
        id: label

        anchors.left: icon.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.width - 58 - (worktreeText.visible ? worktreeText.width + 6 : 0))
        text: root.title
        elide: Text.ElideMiddle
        color: root.active ? root.theme.appText : root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textTabSize
        font.weight: root.active ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
    }

    // The worktree a document belongs to, when it is not the main one.
    Text {
        id: worktreeText

        anchors.left: label.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        visible: root.worktreeLabel.length > 0
        text: root.worktreeLabel
        color: root.theme.accentBright
        opacity: 0.85
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textMetadataSize
    }

    MouseArea {
        id: tabArea

        property point pressPoint
        property bool dragging: false

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        onPressed: function(mouse) {
            pressPoint = Qt.point(mouse.x, mouse.y)
            dragging = false
            if (mouse.button === Qt.LeftButton) {
                Docking.activateView(root.viewId)
            }
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !(pressedButtons & Qt.LeftButton)) {
                return
            }
            if (!dragging && Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 6) {
                dragging = root.workspace.beginDrag("view", root.viewId, root.title)
            }
            if (dragging) {
                root.workspace.moveDrag(mapToItem(root.workspace.area, mouse.x, mouse.y))
            }
        }
        onReleased: function(mouse) {
            if (dragging) {
                root.workspace.endDrag()
                dragging = false
                return
            }
            if (mouse.button === Qt.MiddleButton) {
                root.workspace.closeView(root.viewId)
            } else if (mouse.button === Qt.RightButton) {
                tabMenu.popup()
            }
        }
        onCanceled: {
            if (dragging) {
                root.workspace.cancelDrag()
            }
            dragging = false
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) {
                Docking.toggleMaximize(root.anchorId)
            }
        }
    }

    ToolTip.visible: tabArea.containsMouse && !tabArea.pressed && String(details.path || "").length > 0
    ToolTip.delay: 700
    ToolTip.text: String(details.path || "") + (root.worktreeLabel.length > 0 ? "  ·  " + root.worktreeLabel : "")

    // Dirty dot, which turns into the close button on hover.
    Item {
        anchors.right: parent.right
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20

        Rectangle {
            anchors.centerIn: parent
            width: 7
            height: 7
            radius: 3.5
            color: root.theme.accentBright
            visible: root.dirty && !closeButton.hovered && !tabArea.containsMouse
        }

        IconButton {
            id: closeButton

            anchors.fill: parent
            theme: root.theme
            iconName: "close"
            iconSize: 10
            toolTipText: ""
            visible: tabArea.containsMouse || hovered || root.active && !root.dirty
            opacity: tabArea.containsMouse || hovered ? 1 : 0.6
            focusPolicy: Qt.NoFocus
            onClicked: root.workspace.closeView(root.viewId)
        }
    }

    DockMenu {
        id: tabMenu

        theme: root.theme

        DockMenuItem { theme: root.theme; text: "Close"; shortcutText: "Ctrl+W"; onTriggered: root.workspace.closeView(root.viewId) }
        DockMenuItem { theme: root.theme; text: "Close Others"; onTriggered: root.workspace.closeOtherViews(root.viewId) }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem { theme: root.theme; text: "Split Right"; onTriggered: Docking.splitView(root.viewId, root.anchorId, "right") }
        DockMenuItem { theme: root.theme; text: "Split Down"; onTriggered: Docking.splitView(root.viewId, root.anchorId, "bottom") }
        DockMenuItem {
            theme: root.theme
            text: "Open Second View to the Right"
            visible: root.typeId !== "archivist.terminal" && root.typeId !== "archivist.welcome"
            onTriggered: Docking.duplicateView(root.viewId, "right")
        }
        DockMenuItem {
            theme: root.theme
            text: "Maximize Panel"
            shortcutText: "Ctrl+Shift+Enter"
            onTriggered: Docking.toggleMaximize(root.anchorId)
        }
        DockMenuSeparator { theme: root.theme; visible: root.resource.kind === "document" }
        DockMenuItem {
            theme: root.theme
            text: root.typeId === "archivist.editor" ? "Show Rendered" : "Edit Source"
            visible: root.typeId === "archivist.editor" && root.hasRenderedForm
                || root.typeId === "archivist.preview" && Boolean(root.details.textual)
            onTriggered: Docking.setViewType(
                root.viewId,
                root.typeId === "archivist.editor" ? "archivist.preview" : "archivist.editor"
            )
        }
        DockMenuItem {
            theme: root.theme
            text: "Save"
            shortcutText: "Ctrl+S"
            visible: root.documentId.length > 0
            enabled: root.dirty
            onTriggered: Documents.save(root.documentId)
        }
        DockMenuItem {
            theme: root.theme
            text: "Reveal in Library"
            visible: root.resource.kind === "document"
            onTriggered: root.workspace.revealView(root.viewId)
        }
    }
}
