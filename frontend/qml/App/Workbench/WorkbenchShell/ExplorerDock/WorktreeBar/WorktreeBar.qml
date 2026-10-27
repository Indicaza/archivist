import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

// The worktree selector at the bottom of the side panel.
//
// The bar shows the worktree on screen (branch, unsaved and dirty state,
// running agents). Its popup lists every worktree of the repository and
// creates, opens, refreshes and safely removes them. Switching selects the
// worktree's Library; ExplorerDock does the selecting through
// switchRequested so Explorer state is saved the usual way.
Rectangle {
    id: root

    required property var theme

    // libraryId to select; `reconcile` rescans it against the disk after
    // its catalog shows, so changes made while it was off screen appear.
    signal switchRequested(string libraryId, bool reconcile)

    property string mode: "list"
    property string removalPath: ""
    property string removeAfterSwitchPath: ""
    property string removeAfterSwitchLibraryId: ""
    property bool newBranch: true

    readonly property var active: WorktreeStore.activeWorktree || ({})
    readonly property var activeStatus: active.status || null
    readonly property var worktreeList: WorktreeStore.worktrees || []
    readonly property var removalEntry: entryForPath(removalPath)
    readonly property var preview: WorktreeStore.removalPreview || ({})

    function entryForPath(path) {
        var list = worktreeList

        for (var index = 0; index < list.length; index += 1) {
            if (String(list[index].path || "") === String(path || "")) {
                return list[index]
            }
        }

        return null
    }

    function anchorEntry() {
        var list = worktreeList
        var anchorId = String(WorktreeStore.anchorLibraryId || "")

        for (var index = 0; index < list.length; index += 1) {
            if (String(list[index].libraryId || "") === anchorId) {
                return list[index]
            }
        }

        return null
    }

    function openPopup() {
        mode = "list"
        WorktreeStore.clearError()
        WorktreeStore.refresh()
        worktreePopup.open()
    }

    function activate(entry) {
        var libraryId = String(entry.libraryId || "")

        if (libraryId.length > 0 && entry.libraryArchived !== true) {
            root.switchRequested(libraryId, true)
            worktreePopup.close()
            return
        }

        WorktreeStore.openWorktree(String(entry.path || ""))
    }

    function beginRemoval(entry) {
        removalPath = String(entry.path || "")
        mode = "remove"
        WorktreeStore.clearError()
        WorktreeStore.previewRemoval(removalPath)
    }

    function cancelRemoval() {
        WorktreeStore.clearRemovalPreview()
        removalPath = ""
        mode = "list"
    }

    function confirmRemoval() {
        var entry = removalEntry

        if (!entry) {
            cancelRemoval()
            return
        }

        if (entry.active === true) {
            // The worktree on screen can't be removed from under its
            // editors: return to the main checkout first, then remove.
            var anchor = anchorEntry()

            if (!anchor) {
                return
            }

            removeAfterSwitchPath = String(entry.path || "")
            removeAfterSwitchLibraryId = String(entry.libraryId || "")
            root.switchRequested(String(anchor.libraryId), false)
            return
        }

        WorktreeStore.removeWorktree(removalPath)
    }

    function beginCreate() {
        mode = "create"
        newBranch = true
        branchField.text = ""
        baseField.text = ""
        pathField.text = ""
        WorktreeStore.clearError()
        WorktreeStore.loadBranches()
        Qt.callLater(function() { branchField.forceActiveFocus() })
    }

    function submitCreate() {
        var branch = branchField.text.trim()

        if (branch.length === 0 || WorktreeStore.busy) {
            return
        }

        WorktreeStore.createWorktree(
            branch,
            newBranch,
            newBranch ? baseField.text.trim() : "",
            pathField.text.trim()
        )
    }

    function branchSuggestions(query) {
        var branches = WorktreeStore.branches || []
        var needle = String(query || "").toLowerCase()
        var inUse = ({})
        var list = worktreeList
        var matches = []

        for (var worktreeIndex = 0; worktreeIndex < list.length; worktreeIndex += 1) {
            inUse[String(list[worktreeIndex].branch || "")] = true
        }

        for (var index = 0; index < branches.length && matches.length < 6; index += 1) {
            var branch = String(branches[index])

            if (inUse[branch] === true) {
                continue
            }
            if (needle.length === 0 || branch.toLowerCase().indexOf(needle) >= 0) {
                matches.push(branch)
            }
        }

        return matches
    }

    function summaryText() {
        if (WorktreeStore.loading && worktreeList.length === 0) {
            return "Reading worktrees…"
        }

        return String(active.label || "Worktrees")
    }

    visible: WorktreeStore.repository
        && LibraryStore.selectedLibraryId.length > 0
    implicitHeight: visible ? 30 : 0
    color: barMouse.containsMouse || worktreePopup.visible
        ? theme.hoverBg
        : theme.controlSurfaceBg

    Accessible.role: Accessible.Button
    Accessible.name: "Worktree: " + summaryText()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.quietBorder
    }

    MouseArea {
        id: barMouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (worktreePopup.visible) {
                worktreePopup.close()
            } else {
                root.openPopup()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 6

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            name: "git-branch"
            tone: "accent"
            iconSize: 13
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.summaryText()
            color: root.theme.appText
            font.pixelSize: root.theme.typeSize(10)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: WorktreeStore.stale || WorktreeStore.loading
            text: "updating"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(9)
            font.italic: true
        }

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: root.active.running === true
            name: "agent"
            tone: "accent"
            iconSize: 12
            accessibleLabel: "Agent running here"
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: WorktreeStore.unsavedDocumentCount > 0
            text: WorktreeStore.unsavedDocumentCount + " unsaved"
            color: root.theme.warning
            font.pixelSize: root.theme.typeSize(9)
        }

        // Dirty marker: filled when git reports changes, hollow while the
        // status has not been read yet.
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 7
            implicitHeight: 7
            radius: 3.5
            visible: root.activeStatus === null || root.active.dirty === true
            color: root.activeStatus === null
                ? "transparent"
                : Number(root.activeStatus.conflicted || 0) > 0
                    ? root.theme.danger
                    : root.theme.warning
            border.width: root.activeStatus === null ? 1 : 0
            border.color: root.theme.mutedText
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.worktreeList.length > 1
            text: String(root.worktreeList.length)
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(9)
        }

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            name: worktreePopup.visible ? "chevron-down" : "chevron-up"
            tone: worktreePopup.visible ? "accent" : "muted"
            iconSize: 13
            accessibleLabel: "Choose worktree"
        }
    }

    Connections {
        target: WorktreeStore

        function onWorktreeOpened(library, created) {
            var libraryId = String((library || ({})).id || "")

            if (libraryId.length === 0) {
                return
            }

            root.mode = "list"
            worktreePopup.close()
            root.switchRequested(libraryId, true)
        }

        function onWorktreeRemoved(path, archivedLibraryId) {
            if (String(path) === root.removalPath) {
                root.removalPath = ""
                root.mode = "list"
            }
        }

        function onLibraryIdChanged() {
            if (
                root.removeAfterSwitchPath.length === 0
                || String(WorktreeStore.libraryId)
                    === root.removeAfterSwitchLibraryId
            ) {
                return
            }

            var path = root.removeAfterSwitchPath
            root.removeAfterSwitchPath = ""
            root.removeAfterSwitchLibraryId = ""
            WorktreeStore.removeWorktree(path)
        }
    }

    Popup {
        id: worktreePopup

        x: 6
        y: -height - 4
        width: Math.max(root.width - 12, 260)
        height: Math.min(contentColumn.implicitHeight + 16, 440)
        padding: 8
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        onClosed: {
            if (root.mode === "remove" && root.removeAfterSwitchPath.length === 0) {
                root.cancelRemoval()
            }
            root.mode = "list"
        }

        background: Rectangle {
            color: root.theme.surfaceBg
            radius: root.theme.radiusPanel
            border.width: 1
            border.color: root.theme.panelBorder
        }

        ColumnLayout {
            id: contentColumn

            anchors.fill: parent
            spacing: 6

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: root.mode === "create"
                        ? "New worktree"
                        : root.mode === "remove"
                            ? "Remove worktree"
                            : "Worktrees · "
                                + String(
                                    (WorktreeStore.repositoryInfo || ({})).name
                                        || ""
                                )
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                    elide: Text.ElideRight
                }

                IconButton {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    width: 22
                    height: 22
                    theme: root.theme
                    visible: root.mode === "list"
                    iconName: "refresh"
                    iconSize: 12
                    enabled: !WorktreeStore.loading
                    toolTipText: "Refresh worktrees and status"
                    onClicked: WorktreeStore.refresh()
                }

                IconButton {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    width: 22
                    height: 22
                    theme: root.theme
                    visible: root.mode === "list"
                    iconName: "add"
                    iconSize: 12
                    enabled: !WorktreeStore.busy
                    toolTipText: "New worktree…"
                    onClicked: root.beginCreate()
                }

                IconButton {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    width: 22
                    height: 22
                    theme: root.theme
                    visible: root.mode !== "list"
                    iconName: "chevron-left"
                    iconSize: 12
                    toolTipText: "Back"
                    onClicked: {
                        if (root.mode === "remove") {
                            root.cancelRemoval()
                        }
                        root.mode = "list"
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: WorktreeStore.errorMessage.length > 0
                text: WorktreeStore.errorMessage
                color: root.theme.danger
                font.pixelSize: root.theme.typeSize(10)
                wrapMode: Text.Wrap
            }

            // List
            ListView {
                id: worktreeListView

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 300)
                visible: root.mode === "list"
                clip: true
                spacing: 2
                model: root.worktreeList
                boundsBehavior: Flickable.StopAtBounds

                delegate: WorktreeRow {
                    required property var modelData

                    width: worktreeListView.width
                    theme: root.theme
                    entry: modelData
                    onActivated: root.activate(modelData)
                    onRemoveRequested: root.beginRemoval(modelData)
                    onForgetRequested: WorktreeStore.forgetWorktree(
                        String(modelData.libraryId || "")
                    )
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.mode === "list"
                    && String(WorktreeStore.busyAction) === "open"
                text: "Opening worktree…"
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(9)
            }

            // Create
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.mode === "create"
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [
                            { label: "New branch", value: true },
                            { label: "Existing branch", value: false }
                        ]

                        delegate: Button {
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            text: modelData.label
                            checkable: false
                            hoverEnabled: true
                            padding: 0
                            onClicked: root.newBranch = modelData.value

                            contentItem: Text {
                                text: parent.text
                                color: root.newBranch === parent.modelData.value
                                    ? root.theme.appText
                                    : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(10)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: root.newBranch === parent.modelData.value
                                    ? root.theme.activeBg
                                    : parent.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                                border.width: 1
                                border.color: root.newBranch === parent.modelData.value
                                    ? root.theme.accent
                                    : root.theme.quietBorder
                            }
                        }
                    }
                }

                WorktreeField {
                    id: branchField

                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: root.newBranch
                        ? "New branch name"
                        : "Branch to check out"
                    onAccepted: root.submitCreate()
                }

                Flow {
                    Layout.fillWidth: true
                    visible: !root.newBranch
                    spacing: 4

                    Repeater {
                        model: root.newBranch
                            ? []
                            : root.branchSuggestions(branchField.text)

                        delegate: Button {
                            required property var modelData

                            text: String(modelData)
                            hoverEnabled: true
                            leftPadding: 7
                            rightPadding: 7
                            topPadding: 2
                            bottomPadding: 2
                            onClicked: branchField.text = String(modelData)

                            contentItem: Text {
                                text: parent.text
                                color: root.theme.appText
                                font.pixelSize: root.theme.typeSize(9)
                            }

                            background: Rectangle {
                                radius: 8
                                color: parent.hovered
                                    ? root.theme.hoverBg
                                    : root.theme.controlSurfaceBg
                                border.width: 1
                                border.color: root.theme.quietBorder
                            }
                        }
                    }
                }

                WorktreeField {
                    id: baseField

                    Layout.fillWidth: true
                    visible: root.newBranch
                    theme: root.theme
                    placeholderText: "Start from (default: current HEAD)"
                    onAccepted: root.submitCreate()
                }

                WorktreeField {
                    id: pathField

                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: branchField.text.trim().length > 0
                        ? WorktreeStore.suggestedPath(branchField.text)
                        : "Folder (default: next to the repository)"
                    onAccepted: root.submitCreate()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: WorktreeStore.busyAction === "create"
                            ? "Creating…"
                            : root.newBranch
                                ? "Creates the branch and checks it out in its own folder."
                                : "Checks the branch out in its own folder."
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        wrapMode: Text.Wrap
                    }

                    WorktreeActionButton {
                        theme: root.theme
                        text: "CREATE"
                        primary: true
                        enabled: branchField.text.trim().length > 0
                            && !WorktreeStore.busy
                        onClicked: root.submitCreate()
                    }
                }
            }

            // Remove
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.mode === "remove"
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: root.removalEntry
                        ? String(root.removalEntry.label || "")
                        : ""
                    color: root.theme.appText
                    font.pixelSize: root.theme.typeSize(11)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: root.removalPath
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                    elide: Text.ElideMiddle
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.preview.loading === true
                    text: "Checking for uncommitted work…"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                }

                Repeater {
                    model: root.preview.loading === true
                        ? []
                        : (root.preview.blockers || [])

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 6

                        AppIcon {
                            Layout.alignment: Qt.AlignTop
                            name: "warning"
                            tone: "warning"
                            iconSize: 12
                        }

                        Text {
                            Layout.fillWidth: true
                            text: String(modelData.message || modelData.code || "")
                            color: root.theme.appText
                            font.pixelSize: root.theme.typeSize(10)
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.preview.loading !== true
                        && root.preview.allowed === true
                    text: "Removes the folder. The branch"
                        + (root.removalEntry && String(root.removalEntry.branch || "").length > 0
                            ? " " + root.removalEntry.branch
                            : "")
                        + " is kept, and its chats stay in the archive."
                        + (root.removalEntry && root.removalEntry.active === true
                            ? " Archivist switches to the main checkout first."
                            : "")
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item {
                        Layout.fillWidth: true
                    }

                    WorktreeActionButton {
                        theme: root.theme
                        text: "CANCEL"
                        onClicked: root.cancelRemoval()
                    }

                    WorktreeActionButton {
                        theme: root.theme
                        text: WorktreeStore.busyAction === "remove"
                            ? "REMOVING…"
                            : "REMOVE"
                        primary: true
                        danger: true
                        enabled: root.preview.loading !== true
                            && root.preview.allowed === true
                            && !WorktreeStore.busy
                            && root.removeAfterSwitchPath.length === 0
                        onClicked: root.confirmRemoval()
                    }
                }
            }
        }
    }
}
