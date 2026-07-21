import QtQuick
import Archivist.Services 1.0
import "ActivityRail"
import "ExplorerDock"
import "StatusBar"
import "../Workspace"
import "../ChatDock"
import "../ArtifactDrawer"

Rectangle {
    id: root

    required property var theme

    property int activeViewIndex: 0
    property bool explorerOpen: true
    property bool dockAttached: true

    readonly property real explorerWidth: Math.min(
        theme.explorerMaxWidth,
        Math.max(theme.explorerMinWidth, width * 0.17)
    )
    readonly property bool effectiveDockAttached: explorerOpen && dockAttached
    readonly property real dockHeight: theme.chatDockHeaderHeight + theme.chatDockBodyHeight
    readonly property real attachedDockX: theme.activityRailWidth + explorerWidth
    readonly property real centeredDockWidth: Math.min(width - 32, 1120)
    readonly property real workspaceLeftObstruction: theme.activityRailWidth
        + (explorerOpen ? explorerWidth : 0)

    color: theme.surfaceBg
    clip: true

    Workspace {
        id: workspace

        x: 0
        y: 0
        width: parent.width
        height: parent.height - statusBar.height - chatDock.height
        theme: root.theme
        leftObstruction: root.workspaceLeftObstruction
        onContextInspectionRequested: function(messageId) {
            artifactDrawer.openForMessage(messageId)
        }
    }

    ActivityRail {
        id: activityRail

        x: 0
        y: 0
        height: parent.height - statusBar.height
        theme: root.theme
        activeViewIndex: root.activeViewIndex
        panelOpen: root.explorerOpen
        z: 20

        onViewRequested: function(index) {
            if (root.explorerOpen && root.activeViewIndex === index) {
                root.explorerOpen = false
                return
            }

            root.activeViewIndex = index
            root.explorerOpen = true
        }
    }

    ExplorerDock {
        id: explorerDock

        x: theme.activityRailWidth
        y: 0
        width: root.explorerWidth
        height: parent.height - statusBar.height
        visible: root.explorerOpen
        theme: root.theme
        activeViewIndex: root.activeViewIndex
        z: 15
        onCloseRequested: root.explorerOpen = false
    }

    ChatDock {
        id: chatDock

        x: root.effectiveDockAttached
            ? root.attachedDockX
            : Math.max(16, (parent.width - width) / 2)
        y: parent.height - statusBar.height - height
        width: root.effectiveDockAttached
            ? parent.width - root.attachedDockX
            : root.centeredDockWidth
        height: root.dockHeight
        theme: root.theme
        attached: root.effectiveDockAttached
        z: 30

        onDockModeToggleRequested: root.dockAttached = !root.dockAttached
        onMessageSubmitted: function(message) {
            ChatStore.sendMessage(message)
        }

        Behavior on x {
            NumberAnimation { duration: 190; easing.type: Easing.OutCubic }
        }

        Behavior on width {
            NumberAnimation { duration: 190; easing.type: Easing.OutCubic }
        }
    }

    ArtifactDrawer {
        id: artifactDrawer

        x: parent.width - width - 9
        y: Math.max(18, (parent.height - statusBar.height - height) / 2)
        theme: root.theme
        z: 40
    }

    StatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        theme: root.theme
        explorerOpen: root.explorerOpen
        dockAttached: root.effectiveDockAttached
        z: 50
    }
}
