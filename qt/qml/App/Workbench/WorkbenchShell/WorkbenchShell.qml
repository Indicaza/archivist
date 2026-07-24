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
    property bool resizingExplorer: false
    property bool resizingDock: false
    property real explorerWidth: theme.explorerDefaultWidth
    property real dockHeight: theme.chatDockDefaultHeight
    property bool workspaceStateRestored: false

    readonly property real maximumExplorerWidth: Math.max(
        theme.explorerMinWidth,
        Math.min(
            theme.explorerMaxWidth,
            width - theme.activityRailWidth - 420
        )
    )
    readonly property real clampedExplorerWidth: Math.min(
        maximumExplorerWidth,
        Math.max(theme.explorerMinWidth, explorerWidth)
    )
    property real explorerExtent: explorerOpen ? clampedExplorerWidth : 0
    readonly property real maximumDockHeight: Math.max(
        theme.chatDockMinHeight,
        height - theme.statusBarHeight - theme.workspaceMinHeight
    )
    readonly property real clampedDockHeight: Math.min(
        maximumDockHeight,
        Math.max(theme.chatDockMinHeight, dockHeight)
    )
    readonly property real centeredContentLeft: Math.max(
        theme.messageHorizontalInset,
        (width - theme.transcriptContentWidth) / 2
    )
    readonly property real explorerRight: theme.activityRailWidth + explorerExtent
    readonly property bool explorerEncroachesWorkspace: explorerOpen
        && explorerRight + theme.panelCollisionGap >= centeredContentLeft
    readonly property bool effectiveDockAttached: explorerOpen && dockAttached
    readonly property real attachedDockX: explorerRight
    readonly property real centeredDockWidth: Math.min(width - 32, 1120)
    readonly property real centeredDockX: Math.max(
        16,
        (width - centeredDockWidth) / 2
    )
    readonly property real floatingDockX: explorerOpen
        ? Math.max(centeredDockX, explorerRight + theme.panelCollisionGap)
        : centeredDockX
    readonly property real floatingDockWidth: Math.max(
        360,
        Math.min(centeredDockWidth, width - floatingDockX - 16)
    )
    readonly property real workspaceLeftObstruction: explorerEncroachesWorkspace
        ? explorerRight
        : theme.activityRailWidth
    readonly property real workspacePreviewLeftObstruction: explorerRight
    readonly property real workspaceBottomObstruction: effectiveDockAttached
        ? clampedDockHeight
        : 0

    Behavior on explorerExtent {
        enabled: !root.resizingExplorer
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.OutCubic
        }
    }

    function scheduleWorkspaceStateSave() {
        if (!workspaceStateRestored) {
            return
        }

        workspaceStateSaveTimer.restart()
    }

    function saveWorkspaceState() {
        if (!workspaceStateRestored) {
            return
        }

        WorkspaceState.setValue("workspace/shell/activeViewIndex", activeViewIndex)
        WorkspaceState.setValue("workspace/shell/explorerOpen", explorerOpen)
        WorkspaceState.setValue("workspace/shell/explorerWidth", explorerWidth)
        WorkspaceState.setValue("workspace/shell/dockAttached", dockAttached)
        WorkspaceState.setValue("workspace/shell/dockHeight", dockHeight)
    }

    function restoreWorkspaceState() {
        activeViewIndex = Math.max(
            0,
            Math.min(
                4,
                Number(WorkspaceState.value("workspace/shell/activeViewIndex", 0))
            )
        )
        explorerOpen = Boolean(
            WorkspaceState.value("workspace/shell/explorerOpen", true)
        )
        explorerWidth = Number(
            WorkspaceState.value(
                "workspace/shell/explorerWidth",
                theme.explorerDefaultWidth
            )
        )
        dockAttached = Boolean(
            WorkspaceState.value("workspace/shell/dockAttached", true)
        )
        dockHeight = Number(
            WorkspaceState.value(
                "workspace/shell/dockHeight",
                theme.chatDockDefaultHeight
            )
        )
        workspaceStateRestored = true
    }

    function resetExplorerWidth() {
        explorerWidth = theme.explorerDefaultWidth
    }

    function resetDockHeight() {
        dockHeight = theme.chatDockDefaultHeight
    }

    function resizeExplorerTo(pointerX) {
        explorerWidth = Math.min(
            maximumExplorerWidth,
            Math.max(
                theme.explorerMinWidth,
                pointerX - theme.activityRailWidth
            )
        )
    }

    function resizeDockTo(pointerY) {
        dockHeight = Math.min(
            maximumDockHeight,
            Math.max(
                theme.chatDockMinHeight,
                height - theme.statusBarHeight - pointerY
            )
        )
    }

    Timer {
        id: workspaceStateSaveTimer

        interval: 220
        repeat: false
        onTriggered: root.saveWorkspaceState()
    }

    Component.onCompleted: restoreWorkspaceState()
    Component.onDestruction: {
        workspaceStateSaveTimer.stop()
        saveWorkspaceState()
        WorkspaceState.sync()
    }

    onActiveViewIndexChanged: scheduleWorkspaceStateSave()
    onExplorerOpenChanged: scheduleWorkspaceStateSave()
    onExplorerWidthChanged: scheduleWorkspaceStateSave()
    onDockAttachedChanged: scheduleWorkspaceStateSave()
    onDockHeightChanged: scheduleWorkspaceStateSave()

    Connections {
        target: CollectionStore

        function onWorkspaceScopeChanged() {
            LibraryStore.refresh()
            ChatStore.refresh()
        }
    }

    color: theme.workspaceBg
    clip: true

    Workspace {
        id: workspace

        x: 0
        y: 0
        width: parent.width
        height: parent.height
            - statusBar.height
            - root.workspaceBottomObstruction
        theme: root.theme
        leftObstruction: root.workspaceLeftObstruction
        previewLeftObstruction: root.workspacePreviewLeftObstruction
        onContextInspectionRequested: function(messageId) {
            artifactDrawer.openForMessage(messageId)
        }

        Behavior on height {
            enabled: !root.resizingDock

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.OutCubic
            }
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
        width: root.explorerExtent
        height: parent.height - statusBar.height
        visible: width > 0.5
        opacity: root.explorerOpen ? 1 : 0
        theme: root.theme
        activeViewIndex: root.activeViewIndex
        z: 15
        onCloseRequested: root.explorerOpen = false

        Behavior on opacity {
            NumberAnimation { duration: root.theme.motionFast }
        }
    }

    Item {
        id: explorerResizeHandle

        x: root.explorerRight - width / 2
        y: 0
        width: root.theme.resizeHandleThickness
        height: parent.height - statusBar.height
        visible: root.explorerOpen
        z: 25

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: explorerResizeArea.containsMouse || explorerResizeArea.pressed ? 2 : 1
            height: parent.height
            color: explorerResizeArea.containsMouse || explorerResizeArea.pressed
                ? root.theme.accent
                : root.theme.panelBorder
            opacity: explorerResizeArea.containsMouse || explorerResizeArea.pressed
                ? 0.9
                : 0.52

            Behavior on color {
                ColorAnimation { duration: root.theme.motionFast }
            }
        }

        MouseArea {
            id: explorerResizeArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor
            onPressed: root.resizingExplorer = true
            onReleased: root.resizingExplorer = false
            onCanceled: root.resizingExplorer = false
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }

                const point = mapToItem(root, mouse.x, mouse.y)
                root.resizeExplorerTo(point.x)
            }
            onDoubleClicked: root.resetExplorerWidth()
        }
    }

    ChatDock {
        id: chatDock

        x: root.effectiveDockAttached
            ? root.attachedDockX
            : root.floatingDockX
        y: parent.height - statusBar.height - height
        width: root.effectiveDockAttached
            ? parent.width - root.attachedDockX
            : root.floatingDockWidth
        height: root.clampedDockHeight
        theme: root.theme
        attached: root.effectiveDockAttached
        z: 30

        onDockModeToggleRequested: root.dockAttached = !root.dockAttached
        onMessageSubmitted: function(message) {
            ChatStore.sendMessage(message)
        }

        Behavior on x {
            enabled: !root.effectiveDockAttached
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Behavior on width {
            enabled: !root.effectiveDockAttached
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Behavior on height {
            enabled: !root.resizingDock

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.OutCubic
            }
        }
    }

    Item {
        id: dockResizeHandle

        x: chatDock.x
        y: chatDock.y - height / 2
        width: chatDock.width
        height: root.theme.resizeHandleThickness
        z: 35

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: dockResizeArea.containsMouse || dockResizeArea.pressed ? 2 : 1
            color: dockResizeArea.containsMouse || dockResizeArea.pressed
                ? root.theme.accent
                : root.theme.panelBorder
            opacity: dockResizeArea.containsMouse || dockResizeArea.pressed
                ? 0.9
                : 0.58

            Behavior on color {
                ColorAnimation { duration: root.theme.motionFast }
            }
        }

        MouseArea {
            id: dockResizeArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitVCursor
            onPressed: root.resizingDock = true
            onReleased: root.resizingDock = false
            onCanceled: root.resizingDock = false
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }

                const point = mapToItem(root, mouse.x, mouse.y)
                root.resizeDockTo(point.y)
            }
            onDoubleClicked: root.resetDockHeight()
        }
    }

    MouseArea {
        id: artifactDrawerDismissArea

        anchors.fill: parent
        visible: artifactDrawer.open
        acceptedButtons: Qt.LeftButton
        z: 55
        onClicked: artifactDrawer.open = false
    }

    ArtifactDrawer {
        id: artifactDrawer

        x: parent.width - width - 9
        y: Math.max(18, (parent.height - statusBar.height - height) / 2)
        theme: root.theme
        z: 60
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
