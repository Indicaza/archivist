import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "CollectionEditor"
import "."

Item {
    id: root

    required property var theme
    required property Component libraryContent

    signal closeRequested()

    property bool chatsExpanded: true
    property bool worktreesExpanded: false
    property bool resizingChats: false
    property bool resizingWorktrees: false
    property real preferredChatHeight: 170
    property real preferredWorktreeHeight: 92
    property real chatSectionHeight: chatsExpanded ? preferredChatHeight : 36
    property real worktreeSectionHeight: worktreesExpanded ? preferredWorktreeHeight : 34
    property real chatDragStartY: 0
    property real chatDragStartHeight: 0
    property real worktreeDragStartY: 0
    property real worktreeDragStartHeight: 0

    readonly property var collectionOptions: buildCollectionOptions()
    readonly property var scopedLibraries: filteredLibraries()
    readonly property real sectionHandleHeight: 6
    readonly property real minimumLibraryHeight: 180

    Behavior on chatSectionHeight {
        enabled: !root.resizingChats
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.OutCubic
        }
    }

    Behavior on worktreeSectionHeight {
        enabled: !root.resizingWorktrees
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.OutCubic
        }
    }

    function buildCollectionOptions() {
        var options = []
        var collections = CollectionStore.collections || []

        for (var index = 0; index < collections.length; index += 1) {
            options.push(collections[index])
        }

        return options
    }

    function collectionIndexForId(collectionId) {
        var options = collectionOptions || []

        for (var index = 0; index < options.length; index += 1) {
            if (String(options[index].id || "") === String(collectionId || "")) {
                return index
            }
        }

        return -1
    }

    function filteredLibraries() {
        var libraries = LibraryStore.libraries || []

        if (CollectionStore.selectedCollectionId.length === 0) {
            return []
        }

        var filtered = []
        for (var index = 0; index < libraries.length; index += 1) {
            if (CollectionStore.includesLibrary(String(libraries[index].id))) {
                filtered.push(libraries[index])
            }
        }

        return filtered
    }

    function clampSectionHeight(value, otherHeight, minimumHeight) {
        var maximum = Math.max(
            minimumHeight,
            height - 38 - minimumLibraryHeight - sectionHandleHeight * 2 - otherHeight
        )
        return Math.min(maximum, Math.max(minimumHeight, value))
    }

    function resizeWorktrees(pointerY) {
        worktreesExpanded = true
        preferredWorktreeHeight = clampSectionHeight(
            worktreeDragStartHeight + worktreeDragStartY - pointerY,
            chatSectionHeight,
            68
        )
    }

    function resizeChats(pointerY) {
        chatsExpanded = true
        preferredChatHeight = clampSectionHeight(
            chatDragStartHeight + chatDragStartY - pointerY,
            worktreeSectionHeight,
            96
        )
    }

    Component.onCompleted: CollectionStore.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: root.theme.controlSurfaceBg

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
                anchors.leftMargin: 8
                anchors.rightMargin: 5
                spacing: 4

                Text {
                    text: "COLLECTION"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(8)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.65
                }

                ComboBox {
                    id: collectionSelector

                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    model: root.collectionOptions
                    textRole: "path"
                    valueRole: "id"
                    enabled: !CollectionStore.loading
                        && !CollectionStore.mutating
                        && count > 0
                    hoverEnabled: true
                    leftPadding: 8
                    rightPadding: 22

                    Binding {
                        target: collectionSelector
                        property: "currentIndex"
                        value: root.collectionIndexForId(
                            CollectionStore.selectedCollectionId
                        )
                    }

                    onActivated: function(index) {
                        var collection = root.collectionOptions[index]
                        if (collection) {
                            CollectionStore.selectCollection(
                                String(collection.id || "")
                            )
                        }
                    }

                    contentItem: Text {
                        text: collectionSelector.displayText.length > 0
                            ? collectionSelector.displayText
                            : CollectionStore.loading
                                ? "Loading Collections…"
                                : CollectionStore.collections.length === 0
                                    ? "Create a Collection"
                                    : "Select Collection"
                        color: root.theme.appText
                        font.family: root.theme.titleFontFamily
                        font.pixelSize: root.theme.typeSize(11)
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }

                    indicator: Text {
                        x: parent.width - width - 7
                        y: (parent.height - height) / 2
                        text: "⌄"
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(12)
                    }

                    background: Rectangle {
                        radius: 5
                        color: collectionSelector.hovered
                            || collectionSelector.popup.visible
                                ? root.theme.hoverBg
                                : root.theme.surfaceBg
                        border.width: 1
                        border.color: collectionSelector.popup.visible
                            ? "#554a7b"
                            : root.theme.quietBorder

                        Behavior on color {
                            ColorAnimation {
                                duration: root.theme.motionFast
                            }
                        }
                    }

                    popup: Popup {
                        y: collectionSelector.height + 3
                        width: collectionSelector.width
                        implicitHeight: Math.min(
                            contentItem.implicitHeight + 8,
                            300
                        )
                        padding: 4

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: collectionSelector.popup.visible
                                ? collectionSelector.delegateModel
                                : null
                            currentIndex: collectionSelector.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator {}
                        }

                        background: Rectangle {
                            radius: 6
                            color: root.theme.controlSurfaceBg
                            border.width: 1
                            border.color: root.theme.panelBorder
                        }
                    }

                    delegate: ItemDelegate {
                        id: collectionOption

                        required property int index
                        required property var modelData

                        width: collectionSelector.width - 8
                        height: 34
                        highlighted: collectionSelector.highlightedIndex === index
                        leftPadding: 9
                        rightPadding: 9

                        contentItem: Text {
                            text: String(
                                collectionOption.modelData.path
                                    || collectionOption.modelData.name
                                    || "Collection"
                            )
                            color: root.theme.appText
                            font.pixelSize: root.theme.typeSize(10)
                            font.weight: String(
                                collectionOption.modelData.id || ""
                            ) === String(CollectionStore.selectedCollectionId)
                                ? Font.DemiBold
                                : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideMiddle
                        }

                        background: Rectangle {
                            radius: 4
                            color: collectionOption.highlighted
                                ? root.theme.hoverBg
                                : "transparent"
                        }
                    }
                }

                Button {
                    id: createCollectionButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    text: "+"
                    enabled: !CollectionStore.mutating
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Create Collection"
                    onClicked: collectionEditor.openForCreate()
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(13)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                    }
                }

                Button {
                    id: manageCollectionButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    text: "•••"
                    visible: CollectionStore.selectedCollectionId.length > 0
                    enabled: !CollectionStore.mutating
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Manage Collection"
                    onClicked: collectionEditor.openForEdit(CollectionStore.selectedCollection)
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(8)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                    }
                }

                Button {
                    id: closeExplorerButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    text: "‹"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Close Explorer"
                    onClicked: root.closeRequested()
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(16)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.minimumLibraryHeight
            clip: true

            Loader {
                anchors.fill: parent
                sourceComponent: root.libraryContent
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.sectionHandleHeight
            z: 20

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: worktreeResizeArea.containsMouse || worktreeResizeArea.pressed ? 2 : 1
                color: worktreeResizeArea.containsMouse || worktreeResizeArea.pressed
                    ? root.theme.accent
                    : root.theme.quietBorder
                opacity: worktreeResizeArea.containsMouse || worktreeResizeArea.pressed ? 0.9 : 0.55
            }

            MouseArea {
                id: worktreeResizeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitVCursor
                onPressed: function(mouse) {
                    root.resizingWorktrees = true
                    root.worktreeDragStartHeight = root.worktreeSectionHeight
                    root.worktreeDragStartY = mapToItem(root, mouse.x, mouse.y).y
                }
                onReleased: root.resizingWorktrees = false
                onCanceled: root.resizingWorktrees = false
                onPositionChanged: function(mouse) {
                    if (pressed) {
                        root.resizeWorktrees(mapToItem(root, mouse.x, mouse.y).y)
                    }
                }
                onDoubleClicked: root.preferredWorktreeHeight = 92
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.worktreeSectionHeight
            Layout.minimumHeight: 34
            color: root.theme.surfaceBg
            clip: true

            Button {
                id: worktreeHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 34
                hoverEnabled: true
                padding: 0
                onClicked: root.worktreesExpanded = !root.worktreesExpanded
                contentItem: RowLayout {
                    spacing: 7
                    Text {
                        Layout.leftMargin: 10
                        text: root.worktreesExpanded ? "⌄" : "›"
                        color: worktreeHeader.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(12)
                    }
                    Text {
                        text: "⑂"
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(11)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "WORKTREES"
                        color: worktreeHeader.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.Bold
                        font.letterSpacing: 0.55
                    }
                    Text {
                        Layout.rightMargin: 9
                        text: "COMING NEXT"
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(7)
                        font.weight: Font.Bold
                        font.letterSpacing: 0.4
                        opacity: 0.55
                    }
                }
                background: Rectangle {
                    color: worktreeHeader.hovered ? root.theme.hoverBg : "transparent"
                    Behavior on color { ColorAnimation { duration: root.theme.motionFast } }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: worktreeHeader.bottom
                anchors.leftMargin: 32
                anchors.rightMargin: 10
                anchors.topMargin: 7
                visible: root.worktreesExpanded
                text: "Feature worktrees and swarm status will live here."
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(8)
                opacity: 0.68
                wrapMode: Text.Wrap
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.sectionHandleHeight
            z: 20

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: chatResizeArea.containsMouse || chatResizeArea.pressed ? 2 : 1
                color: chatResizeArea.containsMouse || chatResizeArea.pressed
                    ? root.theme.accent
                    : root.theme.quietBorder
                opacity: chatResizeArea.containsMouse || chatResizeArea.pressed ? 0.9 : 0.55
            }

            MouseArea {
                id: chatResizeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitVCursor
                onPressed: function(mouse) {
                    root.resizingChats = true
                    root.chatDragStartHeight = root.chatSectionHeight
                    root.chatDragStartY = mapToItem(root, mouse.x, mouse.y).y
                }
                onReleased: root.resizingChats = false
                onCanceled: root.resizingChats = false
                onPositionChanged: function(mouse) {
                    if (pressed) {
                        root.resizeChats(mapToItem(root, mouse.x, mouse.y).y)
                    }
                }
                onDoubleClicked: root.preferredChatHeight = 170
            }
        }

        ChatBand {
            Layout.fillWidth: true
            Layout.preferredHeight: root.chatSectionHeight
            Layout.minimumHeight: 36
            theme: root.theme
            expanded: root.chatsExpanded
            clip: true
            onToggleRequested: root.chatsExpanded = !root.chatsExpanded
            onExpandRequested: root.chatsExpanded = true
        }
    }

    CollectionEditor {
        id: collectionEditor
        theme: root.theme
    }
}
