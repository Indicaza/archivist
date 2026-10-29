import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "../Workspace/FilePreview"
import "../IdeHost"

// The content of one view, positioned over its Anchor's content area. It is
// keyed by view, so moving the view between Anchors in this window moves
// this item and keeps its state (scroll position, terminal page).
// Editors are not drawn here: they share their Anchor's Monaco page.
Item {
    id: root

    required property var theme
    required property var workspace
    required property string viewId
    required property string typeId
    required property string title
    required property string resourceKind
    required property string resourceId
    required property string collectionId
    required property string libraryId
    required property var details
    required property var viewState
    required property string anchorId
    required property string surfaceId
    property Item parking: null
    // The window that shows this view. Moving a view to another window
    // reparents this item, so its state (a terminal's page) goes along.
    readonly property var surfaceArea: root.workspace.surfaceAreas[root.surfaceId] || null
    readonly property bool animate: surfaceArea !== null && surfaceArea.animateLayout && !windowJump
    property bool windowJump: false

    parent: surfaceArea || parking
    z: 1
    onSurfaceAreaChanged: {
        windowJump = true
        Qt.callLater(function() { root.windowJump = false })
    }
    required property bool isActive
    required property bool isHidden
    required property real rectX
    required property real rectY
    required property real rectWidth
    required property real rectHeight

    readonly property real headerHeight: root.theme.editorTabHeight
    readonly property bool shown: isActive && !isHidden && typeId !== "archivist.editor"
        && !(typeId === "archivist.chat" && root.workspace.liveChatViewId === root.viewId)
    property bool everShown: false
    onShownChanged: {
        if (shown) {
            everShown = true
        }
        settleTimer.restart()
    }
    Component.onCompleted: {
        everShown = shown
        settle()
    }

    // The content keeps a settled size: it follows the panel once a move or
    // resize comes to rest, not every frame of a spring or drag preview.
    // Terminals especially must never be squeezed (the shell would rewrap
    // its scrollback).
    property real settledWidth: 0
    property real settledHeight: 0

    function settle() {
        if (!shown || width < 80 || height < 40 || Docking.dragActive) {
            return
        }
        settledWidth = width
        settledHeight = height
    }

    onWidthChanged: settleTimer.restart()
    onHeightChanged: settleTimer.restart()

    Timer {
        id: settleTimer

        interval: root.settledWidth > 0 ? 110 : 0
        onTriggered: root.settle()
    }

    Connections {
        target: Docking

        function onDragChanged() {
            if (!Docking.dragActive) {
                settleTimer.restart()
            }
        }
    }

    x: rectX + 1
    y: rectY + headerHeight
    width: Math.max(0, rectWidth - 2)
    height: Math.max(0, rectHeight - headerHeight - 1)
    visible: shown && width > 1 && height > 1
    // Carried away as a picture: not drawn in its old place meanwhile.
    opacity: root.workspace.liftedViewId === root.viewId || root.workspace.liftedAnchorId === root.anchorId ? 0 : 1
    clip: true

    Behavior on x { enabled: root.animate; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on y { enabled: root.animate; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on width { enabled: root.animate; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on height { enabled: root.animate; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }

    Rectangle {
        anchors.fill: parent
        color: root.theme.workspaceBg
    }

    Loader {
        id: content

        width: root.settledWidth > 0 ? root.settledWidth : parent.width
        height: root.settledHeight > 0 ? root.settledHeight : parent.height
        // Created on first show and kept, so switching tabs never reloads.
        active: root.everShown && root.typeId !== "archivist.editor"
        sourceComponent: {
            switch (root.typeId) {
            case "archivist.preview":
                return previewComponent
            case "archivist.terminal":
                return terminalComponent
            case "archivist.chat":
                return chatComponent
            default:
                return placeholderComponent
            }
        }
    }

    // Presses focus the Anchor without taking the press from the content.
    MouseArea {
        anchors.fill: parent
        z: 10
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: function(mouse) {
            Docking.focusAnchor(root.anchorId)
            mouse.accepted = false
        }
    }

    Component {
        id: previewComponent

        FilePreview {
            id: preview

            readonly property bool textual: Boolean(root.details.textual)
            readonly property var snapshot: {
                Documents.revision
                return textual ? Documents.snapshot(root.resourceId) : ({})
            }
            readonly property var fileRecord: root.workspace.fileRecordFor(root.details)

            theme: root.theme
            collectionId: root.collectionId
            libraryId: root.libraryId
            libraryRootPath: root.workspace.libraryRootPath(root.libraryId)
            file: fileRecord
            preview: textual
                ? {
                    content: String(snapshot.text || ""),
                    lineCount: String(snapshot.text || "").split("\n").length,
                    file: fileRecord
                }
                : { file: fileRecord }
            loading: textual && !snapshot.loaded && String(snapshot.error || "").length === 0
            errorMessage: String(snapshot.error || "")
            leftObstruction: 0
            zoomShortcutsEnabled: false

            onViewModeChanged: {
                // "Edit source" turns this tab into the editor for the file.
                if (viewMode === "source") {
                    viewMode = "rendered"
                    Docking.setViewType(root.viewId, "archivist.editor")
                }
            }
        }
    }

    Component {
        id: terminalComponent

        IdeHost {
            theme: root.theme
            activeSurface: "terminal"
            terminalCollectionId: root.collectionId
            terminalLibraryId: root.libraryId
            terminalSessionId: String(root.details.sessionId || "primary")

            onTerminalStateReported: function(sessionId, state, title, cwd) {
                if (title.length > 0 && title !== root.title) {
                    Docking.setViewTitle(root.viewId, title)
                }
            }

            Component.onCompleted: root.workspace.registerTerminal(root.viewId, this)
            Component.onDestruction: root.workspace.unregisterTerminal(root.viewId, this)
        }
    }

    Component {
        id: chatComponent

        // Shown when another view holds the live conversation.
        Item {
            Column {
                anchors.centerIn: parent
                width: Math.min(340, parent.width - 40)
                spacing: 8

                Text {
                    width: parent.width
                    text: root.title.length > 0 ? root.title : "Chat"
                    color: root.theme.appText
                    font.family: root.theme.titleFontFamily
                    font.pixelSize: root.theme.textPanelTitleSize
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: "This conversation is shown in another panel."
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                DockPillButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    theme: root.theme
                    text: "Show it here"
                    iconName: "chat"
                    onClicked: root.workspace.makeChatLive(root.viewId)
                }
            }
        }
    }

    Component {
        id: placeholderComponent

        Item {
            Column {
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 40)
                spacing: 8

                Text {
                    width: parent.width
                    text: root.title.length > 0 ? root.title : "Unavailable view"
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textControlSize
                    font.weight: root.theme.textWeightEmphasis
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: "This kind of view (" + root.typeId + ") is not available in this version of Archivist. "
                        + "The tab is kept so nothing is lost when it comes back."
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
