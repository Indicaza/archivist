import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"
import "../../Files/FileIdentity.js" as FileIdentity

// One tab: one view. Click activates, middle-click closes, dragging moves
// just this view (to another tab position, another Anchor, or an edge).
Item {
    id: root

    objectName: "dock.tab"

    required property var theme
    required property var workspace
    required property string anchorId
    required property string viewId
    required property int tabIndex
    required property bool active
    required property bool anchorFocused
    // A tab held over this strip lands before this one: it steps aside
    // (a transform only; the strip's layout and the model are untouched).
    property real insertShift: 0
    transform: Translate {
        x: root.insertShift
        Behavior on x {
            enabled: !root.theme.reducedMotion
            SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 }
        }
    }

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
        case "archivist.observatory":
            return "history"
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
        // Once it is a magnet, the tab strip can't flick it away.
        preventStealing: true

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
                root.workspace.moveDrag(mapToGlobal(mouse.x, mouse.y))
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
                root.openMenu()
            }
        }
        onCanceled: {
            if (dragging) {
                root.workspace.sourceLost()
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

    // Built the first time it's opened: a tab is cheap to make, and the tab
    // strip changes often (a tab put away or brought back from the Well).
    function openMenu() {
        tabMenuLoader.active = true
        tabMenuLoader.item.popup()
    }

    Loader {
        id: tabMenuLoader

        active: false
        sourceComponent: Component {
            DockMenu {
                id: tabMenu

                // Worked out as the menu opens, not on every layout change.
                property bool canReturn: false
                property bool pinned: false

                theme: root.theme
                onAboutToShow: {
                    canReturn = Docking.canReturnToPrevious("view", root.viewId)
                    pinned = Docking.isViewPinned(root.viewId)
                }

                DockMenuItem { theme: root.theme; text: "Close"; shortcutText: "Ctrl+W"; onTriggered: root.workspace.closeView(root.viewId) }
                DockMenuItem { theme: root.theme; text: "Close Others"; onTriggered: root.workspace.closeOtherViews(root.viewId) }
                DockMenuItem {
                    objectName: "well.minimize"
                    theme: root.theme
                    text: "Minimize to Well"
                    onTriggered: root.workspace.minimizeView(root.viewId)
                }
                DockMenuItem {
                    objectName: "well.pin"
                    theme: root.theme
                    text: tabMenu.pinned ? "Unpin from Well" : "Keep in Well"
                    visible: (root.resource.kind || "").length > 0
                    onTriggered: Docking.setViewPinned(root.viewId, !tabMenu.pinned)
                }
                DockMenuSeparator { theme: root.theme }
                DockMenuItem {
                    objectName: "magnet.returnToPrevious"
                    theme: root.theme
                    text: "Return to Previous Position"
                    visible: tabMenu.canReturn
                    onTriggered: root.workspace.returnToPrevious("view", root.viewId)
                }
                DockMenuItem { theme: root.theme; text: "Split Right"; onTriggered: Docking.splitView(root.viewId, root.anchorId, "right") }
                DockMenuItem { theme: root.theme; text: "Split Down"; onTriggered: Docking.splitView(root.viewId, root.anchorId, "bottom") }
                DockMenuItem {
                    objectName: "magnet.floatView"
                    theme: root.theme
                    text: "Float"
                    visible: !root.workspace.isPanel || root.workspace.isInSidebar || root.workspace.anchorCount > 1 || (Docking.anchor(root.anchorId).views || []).length > 1
                    onTriggered: root.workspace.floatView(root.viewId)
                }
                DockMenuItem {
                    theme: root.theme
                    text: "Move to New Window"
                    visible: !root.workspace.isFloating || root.workspace.isPanel || root.workspace.anchorCount > 1 || (Docking.anchor(root.anchorId).views || []).length > 1
                    onTriggered: root.workspace.detachView(root.viewId)
                }
                DockMenuItem {
                    theme: root.theme
                    text: root.workspace.isPanel ? "Dock in Main Layout" : "Return to Main Window"
                    visible: root.workspace.isFloating
                    onTriggered: root.workspace.returnToMainWindow("view", root.viewId)
                }
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
    }
}
