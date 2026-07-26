import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../IdeHost"

Rectangle {
    id: root

    required property var theme
    required property string collectionId
    required property string libraryId

    property real panelWidth: theme.chatDockPanelDefaultWidth
    property bool resizingPanel: false
    property string activeScopeKey: ""
    property string activeCollectionId: ""
    property string activeLibraryId: ""
    property string activeSessionId: ""
    property string renamingSessionId: ""
    property string renameDraft: ""
    property int nextTerminalNumber: 1
    property bool restoringTerminalState: false
    property real pendingTerminalListContentY: 0

    readonly property real panelMaximumWidth: Math.max(
        180,
        Math.min(
            theme.chatDockPanelMaxWidth,
            width - 280
        )
    )
    readonly property real panelMinimumWidth: Math.min(
        theme.chatDockPanelMinWidth,
        panelMaximumWidth
    )
    readonly property real clampedPanelWidth: Math.min(
        panelMaximumWidth,
        Math.max(panelMinimumWidth, panelWidth)
    )
    readonly property bool terminalContextAvailable:
        collectionId.length > 0
        && libraryId.length > 0

    color: theme.workspaceBg
    clip: true

    function scopeKeyFor(
        targetCollectionId,
        targetLibraryId
    ) {
        var collection = String(
            targetCollectionId || ""
        )
        var library = String(
            targetLibraryId || ""
        )

        if (
            collection.length === 0
            || library.length === 0
        ) {
            return ""
        }

        return collection + ":" + library
    }

    function stateKeyFor(
        targetCollectionId,
        targetLibraryId
    ) {
        return "workspace/collections/"
            + String(targetCollectionId || "")
            + "/libraries/"
            + String(targetLibraryId || "")
            + "/terminals"
    }

    function terminalIndex(sessionId) {
        var targetId = String(sessionId || "")

        for (
            var index = 0;
            index < terminalModel.count;
            index += 1
        ) {
            if (
                String(
                    terminalModel
                        .get(index)
                        .sessionId || ""
                ) === targetId
            ) {
                return index
            }
        }

        return -1
    }

    function terminalTitle(
        baseTitle,
        ordinal,
        customTitle
    ) {
        var custom = String(customTitle || "").trim()

        if (custom.length > 0) {
            return custom
        }

        var base = String(baseTitle || "Terminal")
        var number = Math.max(
            1,
            Number(ordinal || 1)
        )

        return number > 1
            ? base + " " + number
            : base
    }

    function terminalSnapshot() {
        var sessions = []

        for (
            var index = 0;
            index < terminalModel.count;
            index += 1
        ) {
            var terminal = terminalModel.get(index)

            sessions.push({
                sessionId:
                    String(terminal.sessionId || ""),
                title:
                    String(terminal.title || "Terminal"),
                customTitle:
                    String(terminal.customTitle || ""),
                baseTitle:
                    String(
                        terminal.baseTitle || "Terminal"
                    ),
                ordinal:
                    Number(terminal.ordinal || 1),
                terminalState:
                    String(
                        terminal.terminalState
                            || "restored"
                    ),
                cwd:
                    String(terminal.cwd || "")
            })
        }

        return {
            version: 1,
            sessions: sessions,
            activeSessionId:
                String(activeSessionId || ""),
            nextTerminalNumber:
                Number(nextTerminalNumber || 1),
            panelWidth:
                Number(panelWidth || 0),
            listContentY:
                Number(terminalList.contentY || 0)
        }
    }

    function persistActiveScope(syncNow) {
        if (
            restoringTerminalState
            || activeCollectionId.length === 0
            || activeLibraryId.length === 0
        ) {
            return
        }

        WorkspaceState.setValue(
            stateKeyFor(
                activeCollectionId,
                activeLibraryId
            ),
            JSON.stringify(terminalSnapshot())
        )

        if (Boolean(syncNow)) {
            WorkspaceState.sync()
        }
    }

    function scheduleTerminalStateSave() {
        if (restoringTerminalState) {
            return
        }

        terminalStateSaveTimer.restart()
    }

    function restoreTerminalListContentY() {
        terminalList.forceLayout()
        terminalList.contentY = Math.min(
            Math.max(
                terminalList.originY,
                pendingTerminalListContentY
            ),
            Math.max(
                terminalList.originY,
                terminalList.contentHeight
                    - terminalList.height
                    + terminalList.bottomMargin
            )
        )
    }

    function switchTerminalScope() {
        var nextCollectionId =
            String(collectionId || "")
        var nextLibraryId =
            String(libraryId || "")
        var nextScopeKey = scopeKeyFor(
            nextCollectionId,
            nextLibraryId
        )

        if (nextScopeKey === activeScopeKey) {
            return
        }

        persistActiveScope(true)

        restoringTerminalState = true
        renamingSessionId = ""
        renameDraft = ""
        terminalModel.clear()
        activeCollectionId = nextCollectionId
        activeLibraryId = nextLibraryId
        activeScopeKey = nextScopeKey
        activeSessionId = ""
        nextTerminalNumber = 1
        panelWidth =
            theme.chatDockPanelDefaultWidth
        pendingTerminalListContentY = 0

        if (nextScopeKey.length === 0) {
            restoringTerminalState = false
            return
        }

        var missingValue =
            "__archivist_terminal_state_missing__"
        var rawState = WorkspaceState.value(
            stateKeyFor(
                nextCollectionId,
                nextLibraryId
            ),
            missingValue
        )

        if (String(rawState) === missingValue) {
            restoringTerminalState = false
            createTerminal()
            return
        }

        var savedState = ({})

        try {
            savedState = JSON.parse(
                String(rawState || "{}")
            )
        } catch (error) {
            savedState = ({})
        }

        var savedSessions =
            Array.isArray(savedState.sessions)
                ? savedState.sessions
                : []

        for (
            var index = 0;
            index < savedSessions.length
                && index < 32;
            index += 1
        ) {
            var saved = savedSessions[index] || ({})
            var sessionId =
                String(saved.sessionId || "")

            if (
                sessionId.length === 0
                || terminalIndex(sessionId) >= 0
            ) {
                continue
            }

            var ordinal = Math.max(
                1,
                Number(saved.ordinal || index + 1)
            )
            var baseTitle = String(
                saved.baseTitle || "Terminal"
            )
            var customTitle = String(
                saved.customTitle || ""
            )

            terminalModel.append({
                sessionId: sessionId,
                title: terminalTitle(
                    baseTitle,
                    ordinal,
                    customTitle
                ),
                customTitle: customTitle,
                baseTitle: baseTitle,
                ordinal: ordinal,
                terminalState: "restored",
                cwd: String(saved.cwd || "")
            })
        }

        nextTerminalNumber = Math.max(
            1,
            Number(
                savedState.nextTerminalNumber
                    || terminalModel.count + 1
            )
        )
        panelWidth = Number(
            savedState.panelWidth
                || theme.chatDockPanelDefaultWidth
        )
        pendingTerminalListContentY = Math.max(
            0,
            Number(savedState.listContentY || 0)
        )

        var restoredActiveId = String(
            savedState.activeSessionId || ""
        )

        if (
            terminalIndex(restoredActiveId) >= 0
        ) {
            activeSessionId = restoredActiveId
        } else if (terminalModel.count > 0) {
            activeSessionId = String(
                terminalModel.get(0).sessionId || ""
            )
        }

        restoringTerminalState = false
        Qt.callLater(restoreTerminalListContentY)
    }

    function createTerminal() {
        if (!terminalContextAvailable) {
            return
        }

        var ordinal = nextTerminalNumber
        nextTerminalNumber += 1

        var sessionId =
            "terminal-"
            + Date.now().toString(36)
            + "-"
            + ordinal.toString(36)

        terminalModel.append({
            sessionId: sessionId,
            title: terminalTitle(
                "Terminal",
                ordinal,
                ""
            ),
            customTitle: "",
            baseTitle: "Terminal",
            ordinal: ordinal,
            terminalState: "connecting",
            cwd: ""
        })
        activeSessionId = sessionId
        persistActiveScope(true)
    }

    function activateTerminal(sessionId) {
        var targetId = String(sessionId || "")

        if (terminalIndex(targetId) < 0) {
            return
        }

        activeSessionId = targetId
        persistActiveScope(true)
    }

    function beginRename(sessionId) {
        var targetId = String(sessionId || "")
        var index = terminalIndex(targetId)

        if (index < 0) {
            return
        }

        activateTerminal(targetId)
        renamingSessionId = targetId
        renameDraft = String(
            terminalModel.get(index).title
                || "Terminal"
        )
    }

    function cancelRename() {
        renamingSessionId = ""
        renameDraft = ""
    }

    function commitRename(sessionId, value) {
        var targetId = String(sessionId || "")
        var index = terminalIndex(targetId)

        if (index < 0) {
            cancelRename()
            return
        }

        var terminal = terminalModel.get(index)
        var nextCustomTitle =
            String(value || "").trim()

        if (nextCustomTitle.length > 80) {
            nextCustomTitle =
                nextCustomTitle.slice(0, 80)
        }

        terminalModel.setProperty(
            index,
            "customTitle",
            nextCustomTitle
        )
        terminalModel.setProperty(
            index,
            "title",
            terminalTitle(
                terminal.baseTitle,
                terminal.ordinal,
                nextCustomTitle
            )
        )

        cancelRename()
        persistActiveScope(true)
    }

    function closeTerminal(sessionId) {
        var targetId = String(sessionId || "")
        var index = terminalIndex(targetId)

        if (index < 0) {
            return
        }

        if (renamingSessionId === targetId) {
            cancelRename()
        }

        ideHost.killTerminal(targetId)
        terminalModel.remove(index)

        if (activeSessionId === targetId) {
            if (terminalModel.count > 0) {
                var nextIndex = Math.min(
                    index,
                    terminalModel.count - 1
                )
                activeSessionId = String(
                    terminalModel
                        .get(nextIndex)
                        .sessionId || ""
                )
            } else {
                activeSessionId = ""
            }
        }

        persistActiveScope(true)
    }

    function updateTerminalState(
        sessionId,
        state,
        shellTitle,
        cwd
    ) {
        var index = terminalIndex(sessionId)

        if (index < 0) {
            return
        }

        var terminal = terminalModel.get(index)
        var baseTitle = String(
            shellTitle
                || terminal.baseTitle
                || "Terminal"
        )
        var customTitle = String(
            terminal.customTitle || ""
        )

        terminalModel.setProperty(
            index,
            "baseTitle",
            baseTitle
        )
        terminalModel.setProperty(
            index,
            "title",
            terminalTitle(
                baseTitle,
                terminal.ordinal,
                customTitle
            )
        )
        terminalModel.setProperty(
            index,
            "terminalState",
            String(state || "running")
        )
        terminalModel.setProperty(
            index,
            "cwd",
            String(cwd || terminal.cwd || "")
        )
        scheduleTerminalStateSave()
    }

    function resetPanelWidth() {
        panelWidth =
            theme.chatDockPanelDefaultWidth
    }

    function resizePanelTo(pointerX) {
        panelWidth = Math.min(
            panelMaximumWidth,
            Math.max(
                panelMinimumWidth,
                width - pointerX
            )
        )
    }

    Component.onCompleted: {
        switchTerminalScope()
    }

    Component.onDestruction: {
        persistActiveScope(true)
    }

    onCollectionIdChanged:
        Qt.callLater(switchTerminalScope)
    onLibraryIdChanged:
        Qt.callLater(switchTerminalScope)
    onPanelWidthChanged:
        scheduleTerminalStateSave()

    Timer {
        id: terminalStateSaveTimer

        interval: 220
        repeat: false
        onTriggered: {
            root.persistActiveScope(false)
            WorkspaceState.sync()
        }
    }

    ListModel {
        id: terminalModel
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 240
            clip: true

            IdeHost {
                id: ideHost

                anchors.fill: parent
                visible:
                    root.terminalContextAvailable
                    && root.activeSessionId.length > 0
                theme: root.theme
                activeSurface: "terminal"
                terminalCollectionId:
                    root.collectionId
                terminalLibraryId:
                    root.libraryId
                terminalSessionId:
                    root.activeSessionId

                onTerminalStateReported: function(
                    sessionId,
                    state,
                    title,
                    cwd
                ) {
                    root.updateTerminalState(
                        sessionId,
                        state,
                        title,
                        cwd
                    )
                }
            }

            Text {
                anchors.centerIn: parent
                visible:
                    !root.terminalContextAvailable
                    || root.activeSessionId.length === 0
                text: root.terminalContextAvailable
                    ? "No terminals are open."
                    : "Select a Library to open a terminal."
                color: root.theme.mutedText
                font.family:
                    root.theme.bodyFontFamily
                font.pixelSize:
                    root.theme.textControlSize
            }
        }

        Item {
            id: panelResizeHandle

            Layout.preferredWidth:
                root.theme.resizeHandleThickness
            Layout.fillHeight: true
            z: 8

            Rectangle {
                anchors.horizontalCenter:
                    parent.horizontalCenter
                width: 1
                height: parent.height
                color: root.theme.quietBorder
                opacity: 0.82
            }

            Rectangle {
                anchors.centerIn: parent
                width: 3
                height:
                    panelResizeArea.containsMouse
                    || panelResizeArea.pressed
                        ? 32
                        : 18
                color:
                    panelResizeArea.containsMouse
                    || panelResizeArea.pressed
                        ? root.theme.accent
                        : root.theme.panelBorder
                radius: 2
                opacity:
                    panelResizeArea.containsMouse
                    || panelResizeArea.pressed
                        ? 0.95
                        : 0.68

                Behavior on color {
                    ColorAnimation {
                        duration:
                            root.theme.motionFast
                    }
                }

                Behavior on height {
                    NumberAnimation {
                        duration:
                            root.theme.motionFast
                        easing.type:
                            Easing.OutCubic
                    }
                }
            }

            MouseArea {
                id: panelResizeArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                onPressed:
                    root.resizingPanel = true
                onReleased:
                    root.resizingPanel = false
                onCanceled:
                    root.resizingPanel = false
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return
                    }

                    var point = mapToItem(
                        root,
                        mouse.x,
                        mouse.y
                    )
                    root.resizePanelTo(point.x)
                }
                onDoubleClicked:
                    root.resetPanelWidth()
            }
        }

        Rectangle {
            id: terminalPanel

            Layout.preferredWidth:
                root.clampedPanelWidth
            Layout.fillHeight: true
            color: root.theme.sidebarBg
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: root.theme.quietBorder
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 3
                anchors.bottomMargin: 3
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                        opacity: 0.72
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 3
                        spacing: 7

                        Text {
                            text: "TERMINALS"
                            color: root.theme.appText
                            font.family:
                                root.theme.bodyFontFamily
                            font.pixelSize:
                                root.theme
                                    .textSidebarLabelSize
                            font.weight:
                                root.theme.textWeightStrong
                            font.letterSpacing:
                                root.theme
                                    .textTrackingLabel
                        }

                        Text {
                            Layout.fillWidth: true
                            text:
                                String(
                                    terminalModel.count
                                )
                            color: root.theme.mutedText
                            font.family:
                                root.theme.bodyFontFamily
                            font.pixelSize:
                                root.theme.textMetadataSize
                            opacity: 0.76
                        }

                        Button {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            text: "+"
                            enabled:
                                root.terminalContextAvailable
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: enabled
                                ? "New Terminal"
                                : "Select a Library first"
                            onClicked:
                                root.createTerminal()

                            contentItem: Text {
                                text: parent.text
                                color:
                                    parent.enabled
                                    && parent.hovered
                                        ? root.theme.appText
                                        : root.theme
                                            .mutedText
                                font.family:
                                    root.theme
                                        .bodyFontFamily
                                font.pixelSize:
                                    root.theme
                                        .textControlSize
                                horizontalAlignment:
                                    Text.AlignHCenter
                                verticalAlignment:
                                    Text.AlignVCenter
                                opacity:
                                    parent.enabled ? 1 : 0.42
                            }

                            background: Rectangle {
                                radius: 3
                                color: parent.hovered
                                    ? root.theme.hoverBg
                                    : "transparent"
                            }
                        }
                    }
                }

                ListView {
                    id: terminalList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: terminalModel.count > 0
                    spacing: 1
                    topMargin: 4
                    bottomMargin: 4
                    clip: true
                    boundsBehavior:
                        Flickable.StopAtBounds
                    model: terminalModel
                    onContentYChanged:
                        root.scheduleTerminalStateSave()

                    delegate: Rectangle {
                        id: terminalRow

                        required property int index
                        required property string sessionId
                        required property string title
                        required property string customTitle
                        required property string baseTitle
                        required property int ordinal
                        required property string terminalState
                        required property string cwd

                        readonly property bool selected:
                            sessionId
                                === root.activeSessionId
                        width: terminalList.width
                        height: 30
                        radius: 0
                        color:
                            terminalTap.pressed
                                ? root.theme
                                    .controlPressedBg
                                : terminalHover.hovered
                                    ? root.theme.hoverBg
                                    : selected
                                        ? root.theme.activeBg
                                        : "transparent"

                        Behavior on color {
                            ColorAnimation {
                                duration:
                                    root.theme.motionFast
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 2
                            visible: terminalRow.selected
                            color: root.theme.accent
                            z: 4
                        }

                        Text {
                            id: terminalGlyph

                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.verticalCenter:
                                parent.verticalCenter
                            width: 18
                            text: "›_"
                            color:
                                terminalRow.terminalState
                                    === "error"
                                    ? root.theme.danger
                                    : terminalRow.selected
                                        || terminalHover.hovered
                                        ? root.theme
                                            .accentBright
                                        : root.theme
                                            .mutedText
                            font.family:
                                root.theme
                                    .monospaceFontFamily
                            font.pixelSize:
                                root.theme
                                    .textMetadataSize
                            font.weight:
                                root.theme
                                    .textWeightStrong
                            horizontalAlignment:
                                Text.AlignHCenter
                        }

                        Text {
                            id: terminalTitleText

                            anchors.left:
                                terminalGlyph.right
                            anchors.right:
                                closeTerminalButton.left
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            anchors.verticalCenter:
                                parent.verticalCenter
                            visible:
                                root.renamingSessionId
                                    !== terminalRow.sessionId
                            text: terminalRow.title
                            color:
                                terminalRow.selected
                                || terminalHover.hovered
                                    ? root.theme.appText
                                    : root.theme
                                        .mutedText
                            font.family:
                                root.theme
                                    .bodyFontFamily
                            font.pixelSize:
                                root.theme
                                    .textControlSize
                            font.weight:
                                terminalRow.selected
                                    ? root.theme
                                        .textWeightEmphasis
                                    : root.theme
                                        .textWeightRegular
                            elide: Text.ElideRight
                        }

                        TextField {
                            id: terminalRenameField

                            anchors.left:
                                terminalGlyph.right
                            anchors.right:
                                closeTerminalButton.left
                            anchors.leftMargin: 3
                            anchors.rightMargin: 4
                            anchors.verticalCenter:
                                parent.verticalCenter
                            height: 24
                            visible:
                                root.renamingSessionId
                                    === terminalRow.sessionId
                            z: 30
                            padding: 3
                            leftPadding: 4
                            rightPadding: 4
                            color: root.theme.appText
                            selectionColor:
                                root.theme.accentSoft
                            selectedTextColor:
                                root.theme.appText
                            font.family:
                                root.theme.bodyFontFamily
                            font.pixelSize:
                                root.theme.textControlSize
                            maximumLength: 80

                            onVisibleChanged: {
                                if (!visible) {
                                    return
                                }

                                text = root.renameDraft
                                forceActiveFocus()
                                selectAll()
                            }

                            onTextEdited:
                                root.renameDraft = text
                            onEditingFinished: {
                                if (
                                    root.renamingSessionId
                                        === terminalRow.sessionId
                                ) {
                                    root.commitRename(
                                        terminalRow.sessionId,
                                        text
                                    )
                                }
                            }

                            Keys.onReturnPressed:
                                root.commitRename(
                                    terminalRow.sessionId,
                                    text
                                )
                            Keys.onEnterPressed:
                                root.commitRename(
                                    terminalRow.sessionId,
                                    text
                                )
                            Keys.onEscapePressed: {
                                root.cancelRename()
                                event.accepted = true
                            }

                            background: Rectangle {
                                radius: 3
                                color:
                                    root.theme
                                        .controlSurfaceBg
                                border.width: 1
                                border.color:
                                    terminalRenameField
                                        .activeFocus
                                        ? root.theme.accent
                                        : root.theme
                                            .quietBorder
                            }
                        }

                        Button {
                            id: closeTerminalButton

                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            anchors.verticalCenter:
                                parent.verticalCenter
                            width: 24
                            height: 24
                            visible:
                                terminalRow.selected
                                || terminalHover.hovered
                                || hovered
                            text: "×"
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text:
                                "Kill Terminal"
                            onClicked:
                                root.closeTerminal(
                                    terminalRow.sessionId
                                )

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered
                                    ? root.theme.appText
                                    : root.theme
                                        .mutedText
                                font.family:
                                    root.theme
                                        .bodyFontFamily
                                font.pixelSize:
                                    root.theme
                                        .textControlSize
                                font.weight:
                                    root.theme
                                        .textWeightStrong
                                horizontalAlignment:
                                    Text.AlignHCenter
                                verticalAlignment:
                                    Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 3
                                color: parent.hovered
                                    ? root.theme.surfaceBg
                                    : "transparent"
                            }
                        }

                        Item {
                            anchors.left: parent.left
                            anchors.right:
                                closeTerminalButton.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            enabled:
                                root.renamingSessionId
                                    !== terminalRow.sessionId

                            TapHandler {
                                id: terminalTap

                                onTapped:
                                    root.activateTerminal(
                                        terminalRow
                                            .sessionId
                                    )
                                onDoubleTapped:
                                    root.beginRename(
                                        terminalRow
                                            .sessionId
                                    )
                            }
                        }

                        HoverHandler {
                            id: terminalHover
                        }

                        ToolTip {
                            visible:
                                terminalHover.hovered
                                && terminalRow.cwd.length > 0
                                && !closeTerminalButton
                                    .hovered
                                && root.renamingSessionId
                                    !== terminalRow.sessionId
                            delay: 650
                            text:
                                terminalRow.cwd.length > 0
                                    ? terminalRow.cwd
                                        + "\nDouble-click to rename"
                                    : "Double-click to rename"
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: terminalModel.count === 0
                    text:
                        root.terminalContextAvailable
                            ? "No terminals are open."
                            : "Select a Library first."
                    color: root.theme.mutedText
                    font.family:
                        root.theme.bodyFontFamily
                    font.pixelSize:
                        root.theme.textMetadataSize
                    horizontalAlignment:
                        Text.AlignHCenter
                    verticalAlignment:
                        Text.AlignVCenter
                }
            }
        }
    }
}
