import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One Anchor: a dockable tab group. The tab strip moves single views; the
// panel label at its left (name, grip, menu) moves the whole Anchor. The content area below the strip
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
    // Carried as a picture: lifted out of its place (see DockWorkspace).
    readonly property bool lifted: root.workspace.liftedAnchorId === root.anchorId
    opacity: (isVacating || lifted ? 0 : isDragSource ? 0.45 : 1) * arrival

    // A panel that appears in a running workspace eases in (fade and a
    // slight settle) instead of popping; with reduced motion it just appears.
    property real arrival: 1
    property bool arriving: false
    property int arrivalDelay: 0
    // Something just docked here: a small swell and settle (a transform
    // only, so nothing inside it lays out again).
    property real nudge: 0
    transform: Scale {
        origin.x: root.width / 2
        origin.y: root.height / 2
        xScale: 0.985 + 0.015 * root.arrival + root.nudge
        yScale: 0.985 + 0.015 * root.arrival + root.nudge
    }
    function receive(delay) {
        receiveAnimation.stop()
        root.nudge = 0
        if (root.theme.reducedMotion) {
            return
        }
        receivePause.duration = Math.max(0, delay || 0)
        receiveAnimation.start()
    }
    SequentialAnimation {
        id: receiveAnimation
        PauseAnimation { id: receivePause; duration: 0 }
        NumberAnimation {
            target: root; property: "nudge"; to: root.theme.magnetReceiveNudge
            duration: Math.round(root.theme.magnetReceiveDuration * 0.35); easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: root; property: "nudge"; to: 0
            duration: Math.round(root.theme.magnetReceiveDuration * 0.65); easing.type: Easing.OutBack
        }
    }
    SequentialAnimation {
        id: arrivalAnimation
        PauseAnimation { duration: root.arrivalDelay }
        NumberAnimation { target: root; property: "arrival"; to: 1; duration: 220; easing.type: Easing.OutCubic }
        ScriptAction { script: root.arriving = false }
    }

    Behavior on x { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on y { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on width { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on height { enabled: root.workspace.animateLayout; SpringAnimation { spring: root.theme.magnetReflowSpring; damping: root.theme.magnetReflowDamping; epsilon: 0.25 } }
    Behavior on opacity { enabled: !root.arriving; NumberAnimation { duration: root.theme.motionFast } }

    Component.onCompleted: {
        if (root.workspace.animateArrivals === true) {
            root.arriving = true
            root.arrival = 0
            root.arrivalDelay = root.workspace.nextArrivalDelay()
            arrivalAnimation.start()
        }
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
        var x = tabRow.mapFromItem(root, localX, root.headerHeight / 2).x
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

        // The panel label: every panel's name, its handle and its menu, in
        // one place at the left of the header. Drag it to move the panel,
        // double-click (or F2) to rename, click the chevron for the menu.
        Item {
            id: label

            readonly property bool active: labelArea.containsMouse || labelArea.pressed || panelMenu.visible
            readonly property real nameRoom: Math.max(36, root.width * (root.compactHeader ? 0.38 : 0.3))

            x: 3
            // A handle worth grabbing, however short the name.
            width: Math.max(labelRow.implicitWidth + 14, root.compactHeader ? 64 : 92)
            height: parent.height
            Accessible.role: Accessible.Button
            Accessible.name: "Panel " + root.displayName
            Accessible.description: "Drag to move. Double-click to rename. " + root.lockSummary

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 3
                anchors.bottomMargin: 3
                radius: root.theme.radiusSmall
                color: labelArea.pressed || panelMenu.visible ? root.theme.activeBg
                    : labelArea.containsMouse ? root.theme.hoverBg : "transparent"

                Behavior on color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : 90 } }
            }

            Row {
                id: labelRow

                x: 7
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                opacity: root.renaming ? 0 : 1

                // Grip dots: the panel's handle.
                Grid {
                    anchors.verticalCenter: parent.verticalCenter
                    columns: 2
                    rowSpacing: 2.5
                    columnSpacing: 2.5
                    opacity: root.layoutLocked ? 0.25 : label.active ? 0.95 : root.isFocused ? 0.65 : 0.45

                    Repeater {
                        model: 6
                        Rectangle {
                            width: 3.5
                            height: 3.5
                            radius: 1.75
                            color: label.active && !root.layoutLocked ? root.theme.accentBright : root.theme.mutedText
                        }
                    }
                }

                Text {
                    id: nameText

                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, label.nameRoom)
                    text: root.displayName
                    elide: Text.ElideRight
                    // A chosen name reads as a title; an automatic one stays
                    // quiet until the person names the panel.
                    color: root.name.length > 0 || root.isFocused ? root.theme.appText : root.theme.mutedText
                    opacity: root.name.length > 0 ? (root.isFocused ? 1 : 0.86) : (root.isFocused ? 0.82 : 0.7)
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    font.weight: root.name.length > 0 ? root.theme.textWeightEmphasis : Font.Normal
                }

                // The panel's own worktree for new items.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.contextLabel.length > 0 && !root.compactHeader
                    width: Math.min(implicitWidth, 110)
                    text: root.contextLabel
                    elide: Text.ElideRight
                    color: root.theme.accentBright
                    opacity: 0.75
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textMetadataSize
                }

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.locked
                    name: "lock"
                    tone: "muted"
                    iconSize: 11
                    opacity: 0.8
                }

                AppIcon {
                    id: chevron

                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-down"
                    tone: label.active ? "normal" : "muted"
                    iconSize: 10
                    opacity: label.active || root.isFocused ? 0.9 : 0.45
                }
            }

            MouseArea {
                id: labelArea

                property point pressPoint
                property bool dragging: false
                property bool onChevron: false

                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                preventStealing: dragging
                ToolTip.visible: containsMouse && !pressed && !panelMenu.visible && !root.renaming
                ToolTip.delay: 700
                ToolTip.text: (root.lockSummary.length > 0 ? root.lockSummary + "\n" : "")
                    + (root.contextLabel.length > 0 ? "New items open in " + root.contextLabel + "\n" : "")
                    + "Double-click to rename" + (root.layoutLocked ? "" : ", drag to move")

                function openMenu() {
                    panelMenu.popup(label, 0, label.height - 2)
                }

                onPressed: function(mouse) {
                    root.workspace.notePointerFocus()
                    Docking.focusAnchor(root.anchorId)
                    if (mouse.button === Qt.RightButton) {
                        openMenu()
                        return
                    }
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    dragging = false
                    onChevron = mouse.x >= labelRow.x + chevron.x - 4
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !(pressedButtons & Qt.LeftButton) || root.layoutLocked) {
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
                        root.workspace.sourceLost()
                    }
                    dragging = false
                }
                onClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton && onChevron) {
                        openMenu()
                    }
                }
                onDoubleClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton && !onChevron) {
                        root.startRename()
                    }
                }
            }
        }

        Rectangle {
            id: labelDivider

            x: label.x + label.width + 3
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height - 14
            color: root.theme.quietBorder
        }

        TextField {
            id: renameField

            x: label.x + 2
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.max(170, renameMetrics.width + 28), parent.width - x - 8)
            height: Math.min(parent.height - 6, 24)
            visible: root.renaming
            z: 3
            padding: 0
            leftPadding: 6
            rightPadding: 6
            selectByMouse: true
            maximumLength: 80
            placeholderText: String(Docking.anchor(root.anchorId).automaticName || "Panel")
            ToolTip.visible: root.renaming
            ToolTip.text: "Enter to save, Esc to cancel. Leave empty for the automatic name."
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
            TextMetrics {
                id: renameMetrics
                font: renameField.font
                text: renameField.text.length > 0 ? renameField.text : renameField.placeholderText
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

            anchors.left: labelDivider.right
            anchors.leftMargin: 2
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
                preventStealing: dragging
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
                        // Tabs after the insertion point step aside.
                        insertShift: insertMarker.shown && index >= Docking.dropTarget.index ? root.theme.magnetTabGap : 0
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
                        width: Math.min(implicitWidth, Math.max(40, tabFlick.width - 8 - (root.viewIds.length > 1 && !root.compactHeader ? 84 : 56)))
                        text: root.activeViewId.length > 0 ? root.workspace.viewTitle(root.activeInfo) : "Empty shelf"
                        elide: Text.ElideMiddle
                        color: root.activeViewId.length > 0 ? root.theme.appText : root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textTabSize
                        font.weight: root.theme.textWeightEmphasis
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.viewIds.length > 1 && !root.compactHeader
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
                // In the middle of the gap the tabs open up.
                x: !targetTab
                    ? 0
                    : Docking.dropTarget.index >= tabRepeater.count
                        ? targetTab.x + targetTab.width + root.theme.magnetTabGap / 2 - 1
                        : targetTab.x + root.theme.magnetTabGap / 2 - 1
                color: root.theme.accentBright
                radius: 1

                Behavior on x { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
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

    // The common actions first; everything else under More.
    DockMenu {
        id: panelMenu

        // Worked out as the menu opens, not on every layout change.
        property bool canReturn: false

        theme: root.theme
        onAboutToShow: canReturn = Docking.canReturnToPrevious("anchor", root.anchorId)

        DockMenuItem {
            theme: root.theme
            text: "Rename…"
            shortcutText: "F2"
            onTriggered: root.startRename()
        }
        DockMenuItem {
            theme: root.theme
            text: "Lock in Place"
            shortcutText: "position and size"
            checkable: true
            checked: root.layoutLocked
            onTriggered: Docking.setAnchorLayoutLocked(root.anchorId, !root.layoutLocked)
        }
        DockMenuItem {
            theme: root.theme
            text: root.switchable ? "Show as Tabs" : "Show as Shelf"
            onTriggered: Docking.setAnchorSwitchable(root.anchorId, !root.switchable)
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem { theme: root.theme; text: "Split Right"; shortcutText: "Ctrl+\\"; enabled: !root.layoutLocked; onTriggered: root.workspace.splitAnchor(root.anchorId, "right") }
        DockMenuItem { theme: root.theme; text: "Split Down"; shortcutText: "Ctrl+Shift+\\"; enabled: !root.layoutLocked; onTriggered: root.workspace.splitAnchor(root.anchorId, "bottom") }
        DockMenuItem {
            theme: root.theme
            text: root.isMaximized ? "Restore Layout" : "Maximize"
            shortcutText: "Ctrl+Shift+Enter"
            onTriggered: Docking.toggleMaximize(root.anchorId)
        }
        DockMenuItem {
            objectName: "magnet.returnToPrevious"
            theme: root.theme
            text: "Return to Previous Position"
            visible: panelMenu.canReturn
            onTriggered: root.workspace.returnToPrevious("anchor", root.anchorId)
        }
        DockMenuSeparator { theme: root.theme }

        DockMenu {
            id: moreMenu

            theme: root.theme
            title: "More"
            width: 280

            DockMenuItem {
                theme: root.theme
                text: "Use Automatic Name"
                visible: root.name.length > 0
                onTriggered: Docking.renameAnchor(root.anchorId, "")
            }
            DockMenuItem {
                theme: root.theme
                text: "Lock Content"
                shortcutText: "keeps its items"
                checkable: true
                checked: root.contentLocked
                onTriggered: Docking.setAnchorContentLocked(root.anchorId, !root.contentLocked)
            }
            DockMenuItem {
                id: docsPurposeItem

                // Re-read when preferences change.
                readonly property bool forDocs: Docking.workspacePreferences, Docking.anchorPurpose(root.anchorId) === "documentation"

                theme: root.theme
                text: "Use for Documentation"
                shortcutText: "reference opens here"
                checkable: true
                checked: forDocs
                onTriggered: {
                    if (forDocs) {
                        var preferences = Docking.workspacePreferences
                        for (var i = 0; i < preferences.length; ++i) {
                            if (preferences[i].key === "area_purpose" && preferences[i].anchorId === root.anchorId) {
                                Docking.forgetPreference(preferences[i].id)
                            }
                        }
                    } else {
                        Docking.setPreference("area_purpose", "documentation", root.anchorId, true)
                    }
                }
            }
            DockMenuItem {
                theme: root.theme
                text: root.isTarget ? "Stop Opening New Files Here" : "Open New Files Here"
                onTriggered: Docking.toggleTargetAnchor(root.anchorId)
            }

            // Where new terminals (and other new items) opened from this panel go.
            DockMenu {
                id: worktreeMenu

                theme: root.theme
                title: "New Items Open In"
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
                    model: LibraryStore.libraries || []
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

            DockMenuSeparator { theme: root.theme }
            DockMenuItem {
                objectName: "magnet.floatAnchor"
                theme: root.theme
                text: "Float Panel"
                visible: root.viewIds.length > 0 && (!root.workspace.isPanel || root.workspace.isInSidebar || root.workspace.anchorCount > 1)
                enabled: !root.layoutLocked
                onTriggered: root.workspace.floatAnchor(root.anchorId)
            }
            DockMenuItem {
                theme: root.theme
                text: "Move Panel to New Window"
                visible: root.viewIds.length > 0 && (!root.workspace.isFloating || root.workspace.isPanel || root.workspace.anchorCount > 1)
                enabled: !root.layoutLocked
                onTriggered: root.workspace.detachAnchor(root.anchorId)
            }
            DockMenuItem {
                theme: root.theme
                text: root.workspace.isPanel ? "Dock Panel in Main Layout" : "Return Panel to Main Window"
                visible: root.workspace.isFloating
                enabled: !root.layoutLocked
                onTriggered: root.workspace.returnToMainWindow("anchor", root.anchorId)
            }
        }

        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            theme: root.theme
            text: root.layoutLocked ? "Close Panel (locked in place)" : "Close Panel"
            enabled: !root.layoutLocked
            onTriggered: root.workspace.closeAnchor(root.anchorId)
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
            text: root.name.length > 0 ? root.name
                : root.switchable ? "Empty shelf" : root.isTarget ? "New files will open here" : "Empty panel"
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
                : root.name.length > 0 && root.isTarget
                    ? "New files will open here. Drag a tab in, or open a chat or terminal."
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
        // Its tab is carried away: the editor isn't drawn here meanwhile.
        opacity: root.workspace.liftedViewId.length > 0 && root.workspace.liftedViewId === root.activeViewId ? 0 : 1

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
