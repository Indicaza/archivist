import QtQuick
import Archivist.Services 1.0
import "."

Item {
    id: root

    required property var theme
    required property Component libraryContent

    property bool collectionsExpanded: true
    property bool chatsExpanded: true
    property bool librariesExpanded: true
    property bool resizingCollections: false
    property bool resizingChats: false
    property real preferredCollectionContentHeight: 112
    property real preferredChatContentHeight: 174

    readonly property real headerHeight: 36
    readonly property real dividerHeight: 7
    readonly property real minimumContentHeight: 68
    readonly property real bodyBudget: Math.max(
        0,
        height - headerHeight * 3 - dividerHeight * 2
    )
    readonly property real minimumChatReserve: chatsExpanded
        ? minimumContentHeight
        : 0
    readonly property real minimumLibraryReserve: librariesExpanded
        ? minimumContentHeight
        : 0
    readonly property real collectionContentMaximum: Math.max(
        0,
        bodyBudget - minimumChatReserve - minimumLibraryReserve
    )
    readonly property real collectionContentHeight: !collectionsExpanded
        ? 0
        : !chatsExpanded && !librariesExpanded
            ? bodyBudget
            : Math.min(
                collectionContentMaximum,
                Math.max(
                    Math.min(minimumContentHeight, collectionContentMaximum),
                    preferredCollectionContentHeight
                )
            )
    readonly property real chatContentMaximum: Math.max(
        0,
        bodyBudget
            - collectionContentHeight
            - minimumLibraryReserve
    )
    readonly property real chatContentHeight: !chatsExpanded
        ? 0
        : !librariesExpanded
            ? Math.max(0, bodyBudget - collectionContentHeight)
            : Math.min(
                chatContentMaximum,
                Math.max(
                    Math.min(minimumContentHeight, chatContentMaximum),
                    preferredChatContentHeight
                )
            )
    readonly property real libraryContentHeight: librariesExpanded
        ? Math.max(
            0,
            bodyBudget - collectionContentHeight - chatContentHeight
        )
        : 0
    readonly property real collectionSectionHeight: headerHeight
        + collectionContentHeight
    readonly property real chatSectionHeight: headerHeight + chatContentHeight
    readonly property real librarySectionHeight: headerHeight
        + libraryContentHeight
    readonly property var scopedLibraries: filteredLibraries()

    function filteredLibraries() {
        var libraries = LibraryStore.libraries || []

        if (CollectionStore.selectedCollectionId.length === 0) {
            return libraries
        }

        var filtered = []
        for (var index = 0; index < libraries.length; index += 1) {
            if (CollectionStore.includesLibrary(String(libraries[index].id))) {
                filtered.push(libraries[index])
            }
        }

        return filtered
    }

    function resizeCollectionsTo(pointerY) {
        preferredCollectionContentHeight = Math.min(
            collectionContentMaximum,
            Math.max(
                Math.min(minimumContentHeight, collectionContentMaximum),
                pointerY - headerHeight
            )
        )
    }

    function resizeChatsTo(pointerY) {
        preferredChatContentHeight = Math.min(
            chatContentMaximum,
            Math.max(
                Math.min(minimumContentHeight, chatContentMaximum),
                pointerY - chatSection.y - headerHeight
            )
        )
    }

    function resetCollectionHeight() {
        preferredCollectionContentHeight = 112
    }

    function resetChatHeight() {
        preferredChatContentHeight = 174
    }

    CollectionBand {
        id: collectionSection

        x: 0
        y: 0
        width: parent.width
        height: root.collectionSectionHeight
        theme: root.theme
        expanded: root.collectionsExpanded
        clip: true
        onToggleRequested: root.collectionsExpanded = !root.collectionsExpanded
        onExpandRequested: root.collectionsExpanded = true

        Behavior on height {
            enabled: !root.resizingCollections

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.OutCubic
            }
        }
    }

    Item {
        id: collectionDivider

        x: 0
        y: collectionSection.y + collectionSection.height
        width: parent.width
        height: root.dividerHeight
        z: 20

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: collectionResizeArea.containsMouse
                || collectionResizeArea.pressed
                    ? 2
                    : 1
            color: collectionResizeArea.containsMouse
                || collectionResizeArea.pressed
                    ? root.theme.accent
                    : root.theme.panelBorder
            opacity: collectionResizeArea.containsMouse
                || collectionResizeArea.pressed
                    ? 0.92
                    : 0.58
        }

        MouseArea {
            id: collectionResizeArea

            anchors.fill: parent
            enabled: root.collectionsExpanded
                && (root.chatsExpanded || root.librariesExpanded)
            hoverEnabled: true
            cursorShape: enabled ? Qt.SplitVCursor : Qt.ArrowCursor
            onPressed: root.resizingCollections = true
            onReleased: root.resizingCollections = false
            onCanceled: root.resizingCollections = false
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }

                var point = mapToItem(root, mouse.x, mouse.y)
                root.resizeCollectionsTo(point.y)
            }
            onDoubleClicked: root.resetCollectionHeight()
        }
    }

    ChatBand {
        id: chatSection

        x: 0
        y: collectionDivider.y + collectionDivider.height
        width: parent.width
        height: root.chatSectionHeight
        theme: root.theme
        expanded: root.chatsExpanded
        clip: true
        onToggleRequested: root.chatsExpanded = !root.chatsExpanded
        onExpandRequested: root.chatsExpanded = true

        Behavior on height {
            enabled: !root.resizingCollections && !root.resizingChats

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.OutCubic
            }
        }
    }

    Item {
        id: chatDivider

        x: 0
        y: chatSection.y + chatSection.height
        width: parent.width
        height: root.dividerHeight
        z: 20

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: chatResizeArea.containsMouse || chatResizeArea.pressed
                ? 2
                : 1
            color: chatResizeArea.containsMouse || chatResizeArea.pressed
                ? root.theme.accent
                : root.theme.panelBorder
            opacity: chatResizeArea.containsMouse || chatResizeArea.pressed
                ? 0.92
                : 0.58
        }

        MouseArea {
            id: chatResizeArea

            anchors.fill: parent
            enabled: root.chatsExpanded && root.librariesExpanded
            hoverEnabled: true
            cursorShape: enabled ? Qt.SplitVCursor : Qt.ArrowCursor
            onPressed: root.resizingChats = true
            onReleased: root.resizingChats = false
            onCanceled: root.resizingChats = false
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }

                var point = mapToItem(root, mouse.x, mouse.y)
                root.resizeChatsTo(point.y)
            }
            onDoubleClicked: root.resetChatHeight()
        }
    }

    Item {
        id: librarySection

        x: 0
        y: chatDivider.y + chatDivider.height
        width: parent.width
        height: root.librarySectionHeight
        clip: true

        Behavior on height {
            enabled: !root.resizingCollections && !root.resizingChats

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.OutCubic
            }
        }

        SidebarBandHeader {
            id: libraryHeader

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.headerHeight
            theme: root.theme
            title: "LIBRARIES"
            glyph: "▣"
            count: LibraryStore.loadingLibraries
                ? 0
                : root.scopedLibraries.length
            expanded: root.librariesExpanded
            primaryVisible: true
            primaryText: LibraryStore.loadingLibraries ? "…" : "↻"
            primaryEnabled: !LibraryStore.loadingLibraries
            primaryToolTip: "Refresh Libraries"
            onToggleRequested: root.librariesExpanded = !root.librariesExpanded
            onPrimaryRequested: LibraryStore.refresh()
        }

        Loader {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: libraryHeader.bottom
            anchors.bottom: parent.bottom
            visible: root.librariesExpanded
            sourceComponent: root.libraryContent
        }
    }
}
