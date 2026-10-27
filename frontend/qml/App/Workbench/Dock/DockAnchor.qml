import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One Anchor: a dockable tab group. The tab strip moves single views; the
// grip at its left moves the whole Anchor. The content area below the strip
// is left to the workspace's view layer, except for editors, which share one
// Monaco page per Anchor (DockEditorHost).
//
// An Anchor can be named, locked (layout and/or content) and switched to a
// shelf, where a compact picker replaces the tabs. All of that lives in the
// native model; this item only shows it and asks for changes, and the model
// says no when a lock forbids one.
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
    required property string name
    required property string displayName
    required property bool layoutLocked
    required property bool contentLocked
    required property bool switchable
    required property string contextLibraryId

    // The header shows a title only when it adds something: a name the
    // person chose, or a shelf (where it labels the picker).
    readonly property bool showTitle: switchable || name.length > 0
    readonly property bool locked: layoutLocked || contentLocked
    readonly property string lockSummary: layoutLocked && contentLocked
        ? "Layout and content locked"
        : layoutLocked ? "Layout locked: stays where it is" : contentLocked ? "Content locked: keeps its items" : ""
    readonly property var activeInfo: {
        Docking.revision
        return root.activeViewId.length > 0 ? Docking.view(root.activeViewId) : ({})
    }
    readonly property string contextLabel: root.contextLibraryId.length > 0
        ? root.workspace.libraryLabel(root.contextLibraryId)
        : ""
    property bool renaming: false

    function startRename() {
        renaming = true
        renameField.text = root.name.length > 0 ? root.name : root.displayName
        renameField.forceActiveFocus()
        renameField.selectAll()
    }

    function finishRename(commit) {
        if (!renaming) {
            return
        }
        renaming = false
        if (commit) {
            Docking.renameAnchor(root.anchorId, renameField.text)
        }
    }

    function openShelfPicker() {
        Docking.focusAnchor(root.anchorId)
        shelfPicker.openBelow(shelfButton)
    }

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
        if (root.switchable) {
            return -1
        }
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
        // Focus is a quiet edge, not a beacon. Being the target for new
        // files gets no highlight at all (only the header's reveal icon).
        border.color: root.isFocused && root.workspace.showFocus
            ? Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.34)
            : root.theme.quietBorder

        Behavior on border.color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : 120 } }
    }

    Rectangle {
        id: header

        x: 1
        y: 1
        width: parent.width - 2
        height: root.headerHeight - 1
        // The focused panel's header is a shade lighter.
        color: root.isFocused && root.workspace.showFocus
            ? Qt.tint(root.theme.controlSurfaceBg, Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.07))
            : root.theme.controlSurfaceBg
        z: 5

        Behavior on color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : 120 } }

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
                        color: !root.layoutLocked && (gripArea.containsMouse || gripArea.pressed)
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        opacity: root.layoutLocked ? 0.25
                            : gripArea.containsMouse || gripArea.pressed ? 1
                            : root.isFocused ? 0.8 : 0.5
                    }
                }
            }

            MouseArea {
                id: gripArea

                property point pressPoint
                property bool dragging: false

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: root.layoutLocked ? Qt.ArrowCursor : dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                ToolTip.visible: containsMouse && !pressed
                ToolTip.delay: 600
                ToolTip.text: root.layoutLocked
                    ? "This panel's layout is locked"
                    : "Drag to move this panel, or out of the window to float it"

                onPressed: function(mouse) {
                    root.workspace.notePointerFocus()
                    if (mouse.button === Qt.RightButton) {
                        Docking.focusAnchor(root.anchorId)
                        panelMenu.popup()
                        return
                    }
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    dragging = false
                    Docking.focusAnchor(root.anchorId)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || root.layoutLocked) {
                        return
                    }
                    if (!dragging && Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 6) {
                        dragging = root.workspace.beginDrag("anchor", root.anchorId, root.workspace.anchorTitle(root.anchorId))
                    }
                    if (dragging) {
                        root.workspace.moveDrag(mapToGlobal(mouse.x, mouse.y))
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

        // Name, worktree binding and lock state. Shown only when set, so a
        // plain panel's header is just its tabs.
        Row {
            id: titleRow

            anchors.left: grip.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            width: visible ? implicitWidth + 6 : 0
            visible: root.showTitle || root.locked || root.contextLabel.length > 0
            opacity: root.renaming ? 0 : 1

            Text {
                id: titleText

                anchors.verticalCenter: parent.verticalCenter
                visible: root.showTitle
                width: Math.min(implicitWidth, Math.max(48, root.width * 0.28))
                text: root.displayName
                elide: Text.ElideRight
                color: root.isFocused ? root.theme.appText : root.theme.mutedText
                opacity: root.isFocused ? 0.92 : 0.78
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
                font.weight: root.theme.textWeightEmphasis
                font.letterSpacing: 0.2

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: function(mouse) {
                        root.workspace.notePointerFocus()
                        Docking.focusAnchor(root.anchorId)
                        if (mouse.button === Qt.RightButton) {
                            panelMenu.popup()
                        }
                    }
                    onDoubleClicked: function(mouse) {
                        if (mouse.button === Qt.LeftButton) {
                            root.startRename()
                        }
                    }
                }
            }

            // The panel's own worktree for new items.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.contextLabel.length > 0 && !root.compactHeader
                width: Math.min(implicitWidth, 120)
                text: root.contextLabel
                elide: Text.ElideRight
                color: root.theme.accentBright
                opacity: 0.8
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize

                HoverHandler { id: contextHover }
                ToolTip.visible: contextHover.hovered
                ToolTip.delay: 600
                ToolTip.text: "New terminals and files from this panel open in " + root.contextLabel
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.locked
                width: 16
                height: 16

                AppIcon {
                    anchors.centerIn: parent
                    name: "lock"
                    tone: lockHover.hovered ? "normal" : "muted"
                    iconSize: 11
                    opacity: lockHover.hovered ? 1 : 0.75
                }

                HoverHandler { id: lockHover }
                TapHandler { onTapped: lockMenu.popup() }
                ToolTip.visible: lockHover.hovered
                ToolTip.delay: 500
                ToolTip.text: root.lockSummary
            }
        }

        TextField {
            id: renameField

            x: titleRow.x
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.max(160, titleRow.width), parent.width - x - 40)
            height: Math.min(parent.height - 6, 24)
            visible: root.renaming
            z: 3
            padding: 0
            leftPadding: 6
            rightPadding: 6
            selectByMouse: true
            maximumLength: 80
            placeholderText: "Panel name (empty for automatic)"
            color: root.theme.appText
            placeholderTextColor: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            background: Rectangle {
                radius: root.theme.radiusSmall
                color: root.theme.surfaceBg
                border.width: 1
                border.color: root.theme.accent
            }
            Keys.onReturnPressed: root.finishRename(true)
            Keys.onEnterPressed: root.finishRename(true)
            Keys.onEscapePressed: root.finishRename(false)
            onActiveFocusChanged: {
                if (!activeFocus) {
                    root.finishRename(true)
                }
            }
        }

        Flickable {
            id: tabFlick

            anchors.left: titleRow.right
            anchors.right: actions.left
            height: parent.height
            contentWidth: root.switchable ? shelfButton.width + 8 : tabRow.width
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            interactive: contentWidth > width

            // Empty header space acts like a title bar: drag it to move the
            // panel, double-click to maximize, right-click for the menu.
            MouseArea {
                property point pressPoint
                property bool dragging: false

                width: Math.max(tabFlick.width, tabRow.width)
                height: parent.height
                z: -1
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
                onPressed: function(mouse) {
                    root.workspace.notePointerFocus()
                    Docking.focusAnchor(root.anchorId)
                    if (mouse.button === Qt.RightButton) {
                        panelMenu.popup()
                        return
                    }
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    dragging = false
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !(pressedButtons & Qt.LeftButton) || root.viewIds.length === 0 || root.layoutLocked) {
                        return
                    }
                    if (!dragging && Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 8) {
                        dragging = root.workspace.beginDrag("anchor", root.anchorId, root.workspace.anchorTitle(root.anchorId))
                    }
                    if (dragging) {
                        root.workspace.moveDrag(mapToGlobal(mouse.x, mouse.y))
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
                onDoubleClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        Docking.toggleMaximize(root.anchorId)
                    }
                }
            }

            Row {
                id: tabRow

                height: parent.height
                spacing: 0
                visible: !root.switchable

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

            // A shelf shows one item and a compact picker for the rest.
            Button {
                id: shelfButton

                visible: root.switchable
                x: 2
                anchors.verticalCenter: parent.verticalCenter
                height: Math.min(parent.height - 6, 26)
                width: Math.max(120, shelfContent.implicitWidth)
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                padding: 0
                Accessible.name: "Switch shelf item"
                Accessible.description: root.activeViewId.length > 0 ? root.workspace.viewTitle(root.activeInfo) : "Empty shelf"
                ToolTip.visible: hovered && !shelfPicker.opened
                ToolTip.delay: 600
                ToolTip.text: root.viewIds.length > 1
                    ? "Switch item (" + (Qt.platform.os === "osx" ? "⌘⌥P" : "Ctrl+Alt+P") + ")"
                    : "Shelf items"
                onClicked: root.openShelfPicker()

                contentItem: Row {
                    id: shelfContent

                    spacing: 6
                    leftPadding: 8
                    rightPadding: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.activeViewId.length > 0
                        name: root.workspace.viewIconName(root.activeInfo)
                        tone: "normal"
                        iconSize: 13
                    }

                    Text {
                        // Sized from the space the header has, never from the
                        // button, so the two don't chase each other.
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, Math.max(40, tabFlick.width - 8 - (root.viewIds.length > 1 ? 84 : 56)))
                        text: root.activeViewId.length > 0 ? root.workspace.viewTitle(root.activeInfo) : "Empty shelf"
                        elide: Text.ElideMiddle
                        color: root.activeViewId.length > 0 ? root.theme.appText : root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textTabSize
                        font.weight: root.theme.textWeightEmphasis
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.viewIds.length > 1
                        text: (root.viewIds.indexOf(root.activeViewId) + 1) + "/" + root.viewIds.length
                        color: root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textMetadataSize
                    }

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-down"
                        tone: shelfButton.hovered || shelfPicker.opened ? "normal" : "muted"
                        iconSize: 11
                    }
                }

                background: Rectangle {
                    radius: root.theme.radiusSmall
                    color: shelfButton.down || shelfPicker.opened ? root.theme.activeBg
                        : shelfButton.hovered ? root.theme.hoverBg : "transparent"
                    border.width: 1
                    border.color: shelfButton.hovered || shelfPicker.opened ? root.theme.panelBorder : root.theme.quietBorder
                }
            }

            Rectangle {
                id: insertMarker

                readonly property bool shown: !root.switchable && Docking.dragActive
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

                visible: !root.compactHeader && !root.layoutLocked
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
                visible: !root.layoutLocked
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
            text: "Rename Panel…"
            onTriggered: root.startRename()
        }
        DockMenuItem {
            theme: root.theme
            text: "Use Automatic Name"
            visible: root.name.length > 0
            onTriggered: Docking.renameAnchor(root.anchorId, "")
        }
        DockMenuItem {
            theme: root.theme
            text: "Lock…"
            shortcutText: root.locked ? "On" : ""
            onTriggered: lockMenu.popup()
        }
        DockMenuItem {
            theme: root.theme
            text: "Show as Shelf"
            checkable: true
            checked: root.switchable
            onTriggered: Docking.setAnchorSwitchable(root.anchorId, !root.switchable)
        }
        DockMenuItem {
            theme: root.theme
            text: "New Items Open In…"
            shortcutText: root.contextLibraryId.length > 0 ? "Set" : ""
            onTriggered: worktreeMenu.popup()
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: root.isTarget ? "Stop Opening New Files Here" : "Open New Files Here"
            onTriggered: Docking.toggleTargetAnchor(root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem { theme: root.theme; text: "Split Right"; shortcutText: "Ctrl+\\"; enabled: !root.layoutLocked; onTriggered: root.workspace.splitAnchor(root.anchorId, "right") }
        DockMenuItem { theme: root.theme; text: "Split Down"; shortcutText: "Ctrl+Shift+\\"; enabled: !root.layoutLocked; onTriggered: root.workspace.splitAnchor(root.anchorId, "bottom") }
        DockMenuItem {
            theme: root.theme
            text: root.isMaximized ? "Restore Layout" : "Maximize Panel"
            shortcutText: "Ctrl+Shift+Enter"
            onTriggered: Docking.toggleMaximize(root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: "Move Panel to New Window"
            visible: root.viewIds.length > 0 && (!root.workspace.isFloating || root.workspace.anchorCount > 1)
            enabled: !root.layoutLocked
            onTriggered: root.workspace.detachAnchor(root.anchorId)
        }
        DockMenuItem {
            theme: root.theme
            text: "Return Panel to Main Window"
            visible: root.workspace.isFloating
            enabled: !root.layoutLocked
            onTriggered: root.workspace.returnToMainWindow("anchor", root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: root.layoutLocked ? "Close Panel (layout locked)" : "Close Panel"
            enabled: !root.layoutLocked
            onTriggered: root.workspace.closeAnchor(root.anchorId)
        }
    }

    // The two locks are independent.
    DockMenu {
        id: lockMenu

        theme: root.theme
        width: 270

        DockMenuItem {
            theme: root.theme
            text: "Lock Layout"
            shortcutText: "position, size"
            checkable: true
            checked: root.layoutLocked
            onTriggered: Docking.setAnchorLayoutLocked(root.anchorId, !root.layoutLocked)
        }
        DockMenuItem {
            theme: root.theme
            text: "Lock Content"
            shortcutText: "keeps its items"
            checkable: true
            checked: root.contentLocked
            onTriggered: Docking.setAnchorContentLocked(root.anchorId, !root.contentLocked)
        }
    }

    // Where new terminals (and other new items) opened from this panel go.
    DockMenu {
        id: worktreeMenu

        theme: root.theme
        width: 280

        DockMenuItem {
            theme: root.theme
            text: "Follow Context"
            shortcutText: "default"
            checkable: true
            checked: root.contextLibraryId.length === 0
            onTriggered: Docking.setAnchorLibrary(root.anchorId, "")
        }
        DockMenuSeparator { theme: root.theme }

        Instantiator {
            model: worktreeMenu.visible ? (LibraryStore.libraries || []) : []
            delegate: DockMenuItem {
                required property var modelData

                theme: root.theme
                text: root.workspace.libraryLabel(String(modelData.id || ""))
                checkable: true
                checked: root.contextLibraryId === String(modelData.id || "")
                onTriggered: Docking.setAnchorLibrary(root.anchorId, String(modelData.id || ""))
            }
            onObjectAdded: function(index, object) { worktreeMenu.insertItem(index + 2, object) }
            onObjectRemoved: function(index, object) { worktreeMenu.removeItem(object) }
        }
    }

    DockShelfPicker {
        id: shelfPicker

        theme: root.theme
        workspace: root.workspace
        anchorId: root.anchorId
        viewIds: root.viewIds
        activeViewId: root.activeViewId
        contentLocked: root.contentLocked
        onShowAsTabsRequested: Docking.setAnchorSwitchable(root.anchorId, false)
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
            text: root.switchable ? "Empty shelf" : root.isTarget ? "New files will open here" : "Empty panel"
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            font.weight: root.theme.textWeightEmphasis
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: root.switchable
                ? "Drag tabs here to keep them one click away."
                : "Drag a tab here, or open a file, chat or terminal."
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
            root.workspace.notePointerFocus()
            Docking.focusAnchor(root.anchorId)
            mouse.accepted = false
        }
    }
}
