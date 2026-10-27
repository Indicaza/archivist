import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One Anchor: a dockable tab group. The tab strip moves single views; the
// grip at its left moves the whole Anchor. The content area below the strip
// is left to the workspace's view layer, except for editors, which share one
// Monaco page per Anchor (DockEditorHost).
Item {
    id: root

    required property var theme
    required property var workspace // DockWorkspace
    required property string anchorId
    required property string surfaceId
    required property real rectX
    required property real rectY
    required property real rectWidth
    required property real rectHeight
    required property var viewIds
    required property string activeViewId
    required property string activeTypeId
    required property bool pinned
    required property bool isFocused
    required property bool isTarget
    required property bool isMaximized
    required property bool isHidden
    required property bool isDragSource
    required property bool isVacating

    readonly property real headerHeight: root.theme.editorTabHeight
    readonly property bool compactHeader: root.width < 330
    readonly property bool hasEditors: {
        Docking.revision
        for (var index = 0; index < viewIds.length; index += 1) {
            var view = Docking.view(viewIds[index])
            if (view.typeId === "archivist.editor") {
                return true
            }
        }
        return false
    }
    property bool editorHostWanted: false
    onHasEditorsChanged: {
        if (hasEditors) {
            editorHostWanted = true
        }
    }

    x: rectX
    y: rectY
    width: Math.max(0, rectWidth)
    height: Math.max(0, rectHeight)
    visible: !isHidden && width > 1 && height > 1
    opacity: isVacating ? 0 : isDragSource ? 0.45 : 1

    Behavior on x { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
    Behavior on y { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
    Behavior on width { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
    Behavior on height { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
    Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }

    Component.onCompleted: {
        root.workspace.registerAnchor(root.anchorId, root)
        editorHostWanted = hasEditors
    }
    Component.onDestruction: root.workspace.unregisterAnchor(root.anchorId, root)

    function viewInfo(viewId) {
        Docking.revision
        return Docking.view(viewId)
    }

    // Tab insertion index for a pointer at `localX` (anchor coordinates).
    function tabIndexAt(localX) {
        var x = localX - tabRow.x + tabFlick.contentX
        for (var index = 0; index < tabRepeater.count; index += 1) {
            var tab = tabRepeater.itemAt(index)
            if (tab && x < tab.x + tab.width / 2) {
                return index
            }
        }
        return tabRepeater.count
    }

    // The editor page is the content for editor views.
    function editorHost() {
        return editorLoader.item
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        color: root.theme.workspaceBg
        border.width: 1
        border.color: root.isFocused && root.workspace.showFocus
            ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.55)
            : root.theme.quietBorder

        Behavior on border.color { ColorAnimation { duration: root.theme.motionFast } }
    }

    Rectangle {
        id: header

        x: 1
        y: 1
        width: parent.width - 2
        height: root.headerHeight - 1
        color: root.theme.controlSurfaceBg
        z: 5

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        // The Anchor handle: drag to move the whole group.
        Item {
            id: grip

            x: 0
            width: 22
            height: parent.height

            Grid {
                anchors.centerIn: parent
                columns: 2
                rowSpacing: 2
                columnSpacing: 2

                Repeater {
                    model: 6
                    Rectangle {
                        width: 2.5
                        height: 2.5
                        radius: 1.25
                        color: gripArea.containsMouse || gripArea.pressed || root.isFocused
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        opacity: gripArea.containsMouse || gripArea.pressed ? 1 : 0.6
                    }
                }
            }

            MouseArea {
                id: gripArea

                property point pressPoint
                property bool dragging: false

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                acceptedButtons: Qt.LeftButton
                ToolTip.visible: containsMouse && !pressed
                ToolTip.delay: 600
                ToolTip.text: "Drag to move this panel"

                onPressed: function(mouse) {
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    dragging = false
                    Docking.focusAnchor(root.anchorId)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return
                    }
                    if (!dragging && Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 6) {
                        dragging = root.workspace.beginDrag("anchor", root.anchorId, root.workspace.anchorTitle(root.anchorId))
                    }
                    if (dragging) {
                        root.workspace.moveDrag(mapToItem(root.workspace.area, mouse.x, mouse.y))
                    }
                }
                onReleased: {
                    if (dragging) {
                        root.workspace.endDrag()
                    }
                    dragging = false
                }
                onCanceled: {
                    if (dragging) {
                        root.workspace.cancelDrag()
                    }
                    dragging = false
                }
                onDoubleClicked: Docking.toggleMaximize(root.anchorId)
            }
        }

        Flickable {
            id: tabFlick

            anchors.left: grip.right
            anchors.right: actions.left
            height: parent.height
            contentWidth: tabRow.width
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            interactive: contentWidth > width

            // Empty header space: double-click maximizes, like a title bar.
            MouseArea {
                width: Math.max(tabFlick.width, tabRow.width)
                height: parent.height
                z: -1
                acceptedButtons: Qt.LeftButton
                onPressed: Docking.focusAnchor(root.anchorId)
                onDoubleClicked: Docking.toggleMaximize(root.anchorId)
            }

            Row {
                id: tabRow

                height: parent.height
                spacing: 0

                Repeater {
                    id: tabRepeater

                    model: root.viewIds

                    delegate: DockTab {
                        required property string modelData
                        required property int index

                        theme: root.theme
                        workspace: root.workspace
                        anchorId: root.anchorId
                        viewId: modelData
                        tabIndex: index
                        active: modelData === root.activeViewId
                        anchorFocused: root.isFocused
                        height: tabRow.height
                    }
                }

                // Insertion marker while a tab hovers this strip.
                Item {
                    width: 1
                    height: 1
                }
            }

            Rectangle {
                id: insertMarker

                readonly property bool shown: Docking.dragActive
                    && Docking.dropAllowed
                    && Docking.dropTarget.anchorId === root.anchorId
                    && Docking.dropTarget.edge === "center"
                    && Docking.dropTarget.index >= 0
                readonly property var targetTab: shown
                    ? tabRepeater.itemAt(Math.min(Docking.dropTarget.index, tabRepeater.count - 1))
                    : null

                visible: shown
                width: 2
                height: parent.height - 8
                y: 4
                x: !targetTab
                    ? 0
                    : Docking.dropTarget.index >= tabRepeater.count
                        ? targetTab.x + targetTab.width - 1
                        : targetTab.x - 1
                color: root.theme.accentBright
                radius: 1

                Behavior on x { enabled: root.workspace.animateLayout; NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
            }
        }

        HoverHandler {
            id: headerHover
        }

        Row {
            id: actions

            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            opacity: headerHover.hovered || root.isFocused || root.viewIds.length === 0 ? 1 : 0.35

            Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }

            // Narrow panels fold their actions into one menu so tabs keep room.
            IconButton {
                id: moreButton

                visible: root.compactHeader
                theme: root.theme
                width: 24
                height: 24
                iconSize: 13
                iconName: "more"
                toolTipText: "Panel actions"
                onClicked: panelMenu.popup(moreButton, 0, moreButton.height + 2)
            }

            IconButton {
                visible: !root.compactHeader
                theme: root.theme
                width: 24
                height: 24
                iconSize: 13
                iconName: "reveal"
                checkable: true
                checked: root.isTarget
                iconTone: root.isTarget ? "accent" : (hovered ? "normal" : "muted")
                toolTipText: root.isTarget
                    ? "New files open here (click to stop)"
                    : "Open new files in this panel"
                onClicked: Docking.toggleTargetAnchor(root.anchorId)
            }

            Button {
                id: splitButton

                visible: !root.compactHeader
                width: 24
                height: 24
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered
                ToolTip.delay: 350
                ToolTip.text: "Split right (Ctrl+\\)"
                onClicked: root.workspace.splitAnchor(root.anchorId, "right")

                contentItem: Item {
                    DockSplitGlyph {
                        anchors.centerIn: parent
                        color: splitButton.hovered ? root.theme.appText : root.theme.mutedText
                    }
                }

                background: Rectangle {
                    radius: root.theme.radiusSmall
                    color: splitButton.down ? root.theme.activeBg : splitButton.hovered ? root.theme.hoverBg : "transparent"
                }
            }

            IconButton {
                visible: !root.compactHeader
                theme: root.theme
                width: 24
                height: 24
                iconSize: 13
                iconName: root.isMaximized ? "collapse" : "expand"
                iconTone: root.isMaximized ? "accent" : (hovered ? "normal" : "muted")
                toolTipText: root.isMaximized ? "Restore layout" : "Maximize panel"
                onClicked: Docking.toggleMaximize(root.anchorId)
            }

            IconButton {
                theme: root.theme
                width: 24
                height: 24
                iconSize: 12
                iconName: "close"
                toolTipText: "Close panel"
                onClicked: root.workspace.closeAnchor(root.anchorId)
            }
        }
    }

    DockMenu {
        id: panelMenu

        theme: root.theme

        DockMenuItem {
            theme: root.theme
            text: root.isTarget ? "Stop Opening New Files Here" : "Open New Files Here"
            onTriggered: Docking.toggleTargetAnchor(root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem { theme: root.theme; text: "Split Right"; shortcutText: "Ctrl+\\"; onTriggered: root.workspace.splitAnchor(root.anchorId, "right") }
        DockMenuItem { theme: root.theme; text: "Split Down"; shortcutText: "Ctrl+Shift+\\"; onTriggered: root.workspace.splitAnchor(root.anchorId, "bottom") }
        DockMenuItem {
            theme: root.theme
            text: root.isMaximized ? "Restore Layout" : "Maximize Panel"
            shortcutText: "Ctrl+Shift+Enter"
            onTriggered: Docking.toggleMaximize(root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem { theme: root.theme; text: "Close Panel"; onTriggered: root.workspace.closeAnchor(root.anchorId) }
    }

    // Empty Anchor.
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.headerHeight / 2
        width: Math.min(300, parent.width - 32)
        spacing: 6
        visible: root.viewIds.length === 0 && parent.height > root.headerHeight + 60

        Text {
            width: parent.width
            text: root.isTarget ? "New files will open here" : "Empty panel"
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            font.weight: root.theme.textWeightEmphasis
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: "Drag a tab here, or open a file, chat or terminal."
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            topPadding: 6

            DockPillButton {
                theme: root.theme
                text: "New terminal"
                iconName: "terminal"
                onClicked: root.workspace.openTerminal(root.anchorId)
            }

            DockPillButton {
                theme: root.theme
                text: "Show chat"
                iconName: "chat"
                onClicked: root.workspace.openChat(root.anchorId)
            }
        }
    }

    Loader {
        id: editorLoader

        x: 1
        y: root.headerHeight
        width: parent.width - 2
        height: Math.max(0, parent.height - root.headerHeight - 1)
        active: root.editorHostWanted
        visible: root.activeTypeId === "archivist.editor"

        sourceComponent: DockEditorHost {
            theme: root.theme
            anchorId: root.anchorId
            viewId: root.activeTypeId === "archivist.editor" ? root.activeViewId : ""
            shown: root.activeTypeId === "archivist.editor" && root.visible
            onFileLocationRequested: function(filePath, lineNumber, columnNumber) {
                root.workspace.openLocation(filePath, lineNumber, columnNumber)
            }
            onPreviewRequested: function(viewId) {
                Docking.setViewType(viewId, "archivist.preview")
            }
        }
    }

    // Presses anywhere in the content focus this Anchor without taking the
    // press from the page underneath.
    MouseArea {
        x: 0
        y: root.headerHeight
        width: parent.width
        height: Math.max(0, parent.height - root.headerHeight)
        z: 10
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: function(mouse) {
            Docking.focusAnchor(root.anchorId)
            mouse.accepted = false
        }
    }
}
