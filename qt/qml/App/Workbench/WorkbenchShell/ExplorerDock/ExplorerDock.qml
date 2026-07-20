import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "ExplorerItem"

Rectangle {
    id: root

    required property var theme
    required property int activeViewIndex

    signal closeRequested()

    property string selectedNodeId: ""
    property string filterText: ""
    property var expandedNodeIds: ({})
    property var treeNodes: []
    property var childrenByParent: ({})
    property var filterMatchCache: ({})
    property string selectedNodePath: ""

    readonly property var viewTitles: [
        "Library Explorer",
        "Archived Libraries",
        "Library Search",
        "Plugins",
        "Tools"
    ]

    color: theme.surfaceBg
    border.width: 0
    clip: true

    function glyphForFile(fileName, extension) {
        var suffix = String(extension || "").toLowerCase()
        var name = String(fileName || "").toLowerCase()

        if (suffix === ".qml" || name.endsWith(".qml")) {
            return "◇"
        }

        if ([".ts", ".tsx", ".js", ".jsx", ".json", ".cpp", ".h", ".hpp"].indexOf(suffix) >= 0) {
            return "{}"
        }

        if ([".md", ".txt", ".log", ".css", ".html"].indexOf(suffix) >= 0) {
            return "▤"
        }

        return "·"
    }

    function rebuildNodesFromFiles() {
        var nodes = []
        var directories = ({})
        var files = LibraryStore.files || []

        for (var fileIndex = 0; fileIndex < files.length; fileIndex += 1) {
            var file = files[fileIndex]
            var relativePath = String(file.relativePath || file.name || "")
            var rawParts = relativePath.split("/")
            var parts = []

            for (var partIndex = 0; partIndex < rawParts.length; partIndex += 1) {
                if (rawParts[partIndex].length > 0) {
                    parts.push(rawParts[partIndex])
                }
            }

            if (parts.length === 0) {
                continue
            }

            var parentId = ""
            var pathParts = []

            for (var directoryIndex = 0; directoryIndex < parts.length - 1; directoryIndex += 1) {
                pathParts.push(parts[directoryIndex])
                var directoryPath = pathParts.join("/")
                var directoryId = "folder:" + directoryPath

                if (directories[directoryId] !== true) {
                    directories[directoryId] = true
                    nodes.push({
                        id: directoryId,
                        parentId: parentId,
                        title: parts[directoryIndex],
                        glyph: "□",
                        folder: true,
                        relativePath: directoryPath,
                        muted: false,
                        warning: false
                    })
                }

                parentId = directoryId
            }

            nodes.push({
                id: "file:" + String(file.id),
                parentId: parentId,
                title: String(file.name || parts[parts.length - 1]),
                glyph: glyphForFile(file.name, file.extension),
                folder: false,
                relativePath: relativePath,
                muted: file.status !== "available",
                warning: file.status !== "available"
            })
        }

        nodes.sort(function(left, right) {
            if (left.parentId !== right.parentId) {
                return left.parentId.localeCompare(right.parentId)
            }

            if (left.folder !== right.folder) {
                return left.folder ? -1 : 1
            }

            return left.title.localeCompare(right.title)
        })

        var groupedChildren = ({})
        for (var groupedIndex = 0; groupedIndex < nodes.length; groupedIndex += 1) {
            var groupedNode = nodes[groupedIndex]
            var groupKey = groupedNode.parentId

            if (!groupedChildren[groupKey]) {
                groupedChildren[groupKey] = []
            }

            groupedChildren[groupKey].push(groupedNode)
        }

        treeNodes = nodes
        childrenByParent = groupedChildren

        var selectedStillExists = false
        for (var nodeIndex = 0; nodeIndex < nodes.length; nodeIndex += 1) {
            if (nodes[nodeIndex].id === selectedNodeId) {
                selectedStillExists = true
                break
            }
        }

        if (!selectedStillExists) {
            selectedNodeId = ""
            selectedNodePath = ""
        }

        rebuildTree()
    }

    function libraryIndexForId(libraryId) {
        var libraries = LibraryStore.libraries || []

        for (var index = 0; index < libraries.length; index += 1) {
            if (String(libraries[index].id) === String(libraryId)) {
                return index
            }
        }

        return -1
    }

    function isExpanded(nodeId) {
        return expandedNodeIds[nodeId] === true
    }

    function nodeMatches(node, query) {
        return query.length === 0
            || node.title.toLowerCase().indexOf(query) !== -1
    }

    function subtreeMatches(nodeId, query) {
        var cacheKey = nodeId + "|" + query

        if (filterMatchCache[cacheKey] !== undefined) {
            return filterMatchCache[cacheKey]
        }

        var children = childrenByParent[nodeId] || []

        for (var index = 0; index < children.length; index += 1) {
            var child = children[index]

            if (nodeMatches(child, query) || subtreeMatches(child.id, query)) {
                filterMatchCache[cacheKey] = true
                return true
            }
        }

        filterMatchCache[cacheKey] = false
        return false
    }

    function appendVisibleChildren(parentId, depth, query) {
        var children = childrenByParent[parentId] || []

        for (var index = 0; index < children.length; index += 1) {
            var node = children[index]
            var includeNode = query.length === 0
                || nodeMatches(node, query)
                || subtreeMatches(node.id, query)

            if (!includeNode) {
                continue
            }

            visibleTree.append({
                nodeId: node.id,
                itemTitle: node.title,
                itemGlyph: node.glyph,
                itemDepth: depth,
                itemSelected: selectedNodeId === node.id,
                itemMuted: node.muted === true,
                itemFolder: node.folder === true,
                itemExpanded: node.folder === true && isExpanded(node.id),
                itemWarning: node.warning === true
            })

            if (node.folder === true && (query.length > 0 || isExpanded(node.id))) {
                appendVisibleChildren(node.id, depth + 1, query)
            }
        }
    }

    function rebuildTree() {
        visibleTree.clear()
        filterMatchCache = ({})
        appendVisibleChildren("", 0, filterText.trim().toLowerCase())
    }

    function activateNode(nodeId, folder) {
        selectedNodeId = nodeId

        for (var index = 0; index < treeNodes.length; index += 1) {
            if (treeNodes[index].id === nodeId) {
                selectedNodePath = String(treeNodes[index].relativePath || treeNodes[index].title)
                break
            }
        }

        if (folder) {
            expandedNodeIds[nodeId] = !isExpanded(nodeId)
        }

        rebuildTree()
    }

    function collapseAll() {
        expandedNodeIds = ({})
        rebuildTree()
    }

    function expandAll() {
        var nextExpanded = ({})

        for (var index = 0; index < treeNodes.length; index += 1) {
            if (treeNodes[index].folder === true) {
                nextExpanded[treeNodes[index].id] = true
            }
        }

        expandedNodeIds = nextExpanded
        rebuildTree()
    }

    Component.onCompleted: {
        rebuildNodesFromFiles()
        LibraryStore.refresh()
    }

    Connections {
        target: LibraryStore

        function onFilesChanged() {
            root.rebuildNodesFromFiles()
        }

        function onSelectedLibraryIdChanged() {
            root.selectedNodeId = ""
            root.selectedNodePath = ""
            root.expandedNodeIds = ({})
            root.rebuildNodesFromFiles()
        }
    }

    ListModel {
        id: visibleTree
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.panelBorder
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.explorerHeaderHeight
            color: "#1a1916"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 5
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.0
                    elide: Text.ElideRight
                }

                Button {
                    Layout.preferredWidth: 25
                    Layout.preferredHeight: 25
                    text: "‹"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.closeRequested()

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 17
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                        border.width: parent.hovered ? 1 : 0
                        border.color: root.theme.panelBorder
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.activeViewIndex === 0 ? libraryView : placeholderView
        }
    }

    Component {
        id: libraryView

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 35
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 4

                        ComboBox {
                            id: librarySelector

                            Layout.fillWidth: true
                            Layout.preferredHeight: 25
                            model: LibraryStore.libraries
                            textRole: "name"
                            valueRole: "id"
                            enabled: !LibraryStore.loadingLibraries && count > 0
                            hoverEnabled: true
                            leftPadding: 7
                            rightPadding: 24

                            Binding {
                                target: librarySelector
                                property: "currentIndex"
                                value: root.libraryIndexForId(LibraryStore.selectedLibraryId)
                            }

                            onActivated: function(index) {
                                var library = LibraryStore.libraries[index]
                                if (library) {
                                    LibraryStore.selectLibrary(String(library.id))
                                }
                            }

                            contentItem: Text {
                                text: librarySelector.displayText.length > 0
                                    ? librarySelector.displayText
                                    : LibraryStore.loadingLibraries
                                        ? "Loading Libraries…"
                                        : "No Libraries"
                                color: root.theme.appText
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            indicator: Text {
                                x: parent.width - width - 7
                                y: (parent.height - height) / 2
                                text: "⌄"
                                color: root.theme.mutedText
                                font.pixelSize: 11
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                                border.width: parent.popup.visible ? 1 : 0
                                border.color: root.theme.panelBorder
                            }

                            popup: Popup {
                                y: librarySelector.height + 3
                                width: librarySelector.width
                                implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
                                padding: 4

                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: librarySelector.popup.visible ? librarySelector.delegateModel : null
                                    currentIndex: librarySelector.highlightedIndex
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
                                required property int index
                                required property var modelData

                                width: librarySelector.width - 8
                                height: 30
                                highlighted: librarySelector.highlightedIndex === index

                                contentItem: Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1

                                    Text {
                                        width: parent.width
                                        text: String(modelData.name || "Library")
                                        color: root.theme.appText
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: String(modelData.rootPath || "")
                                        color: root.theme.mutedText
                                        font.pixelSize: 7
                                        elide: Text.ElideMiddle
                                    }
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.highlighted ? root.theme.hoverBg : "transparent"
                                }
                            }
                        }

                        Button {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "⌃"
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Collapse all"
                            onClicked: root.collapseAll()

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }

                        Button {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "⌄"
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Expand all"
                            onClicked: root.expandAll()

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }

                        Button {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: LibraryStore.loadingLibraries ? "…" : "↻"
                            enabled: !LibraryStore.loadingLibraries
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Refresh Libraries"
                            onClicked: LibraryStore.refresh()

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: 13
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 33
                    color: root.theme.surfaceBg

                    TextField {
                        id: libraryFilter

                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        placeholderText: "Filter files"
                        placeholderTextColor: root.theme.mutedText
                        color: root.theme.appText
                        font.pixelSize: 10
                        leftPadding: 8
                        rightPadding: 8
                        selectByMouse: true
                        onTextChanged: {
                            root.filterText = text
                            root.rebuildTree()
                        }

                        background: Rectangle {
                            radius: 4
                            color: "#0f0e0c"
                            border.width: 1
                            border.color: parent.activeFocus
                                ? "#554a7b"
                                : root.theme.quietBorder
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: root.theme.surfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 7

                        Text {
                            Layout.fillWidth: true
                            text: String(LibraryStore.selectedLibrary.rootPath || "No Library selected")
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }

                        Rectangle {
                            Layout.preferredWidth: 25
                            Layout.preferredHeight: 16
                            radius: 8
                            color: "#26231e"

                            Text {
                                anchors.centerIn: parent
                                text: String(LibraryStore.files.length)
                                color: root.theme.mutedText
                                font.pixelSize: 8
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            Layout.preferredWidth: 23
                            Layout.preferredHeight: 23
                            text: LibraryStore.scanning ? "…" : "↻"
                            enabled: LibraryStore.selectedLibraryId.length > 0 && !LibraryStore.scanning
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Rescan Library"
                            onClicked: LibraryStore.scanSelectedLibrary()

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: 14
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

                    ListView {
                    id: libraryList

                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    anchors.topMargin: 3
                    clip: true
                    spacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 600
                    reuseItems: true
                    model: visibleTree

                    delegate: ExplorerItem {
                        required property string nodeId
                        required property string itemTitle
                        required property string itemGlyph
                        required property int itemDepth
                        required property bool itemSelected
                        required property bool itemMuted
                        required property bool itemFolder
                        required property bool itemExpanded
                        required property bool itemWarning

                        width: libraryList.width
                        theme: root.theme
                        title: itemTitle
                        glyph: itemGlyph
                        depth: itemDepth
                        selected: itemSelected
                        muted: itemMuted
                        folder: itemFolder
                        expanded: itemExpanded
                        warning: itemWarning
                        onActivated: root.activateNode(nodeId, itemFolder)
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: Math.max(120, parent.width - 28)
                        visible: visibleTree.count === 0
                        text: LibraryStore.errorMessage.length > 0
                            ? LibraryStore.errorMessage + "\n\nRun npm run dev:backend if the local API is offline."
                            : LibraryStore.loadingLibraries
                                ? "Loading Libraries…"
                                : LibraryStore.loadingFiles
                                    ? "Loading files…"
                                    : LibraryStore.selectedLibraryId.length === 0
                                        ? "No Library selected"
                                        : "No cataloged files yet.\nUse Rescan Library to refresh the catalog."
                        color: LibraryStore.errorMessage.length > 0
                            ? root.theme.danger
                            : root.theme.mutedText
                        font.pixelSize: 10
                        lineHeight: 1.35
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: LibraryStore.errorMessage.length > 0
                                ? LibraryStore.errorMessage
                                : root.selectedNodePath.length > 0
                                    ? "Selected  ·  " + root.selectedNodePath
                                    : LibraryStore.scanning
                                        ? "Scanning Library…"
                                        : LibraryStore.loadingFiles
                                            ? "Loading file catalog…"
                                            : LibraryStore.latestScan.status
                                                ? "Catalog  ·  " + String(LibraryStore.latestScan.status)
                                                : "Ready  ·  API connected"
                            color: LibraryStore.errorMessage.length > 0
                                ? root.theme.danger
                                : root.theme.mutedText
                            font.pixelSize: 8
                            opacity: 0.72
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Component {
        id: placeholderView

        Item {
            Column {
                anchors.centerIn: parent
                width: Math.max(140, parent.width - 30)
                spacing: 10

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 46
                    height: 46
                    radius: 13
                    color: root.theme.accentSoft
                    border.width: 1
                    border.color: "#554a7b"

                    Text {
                        anchors.centerIn: parent
                        text: root.activeViewIndex === 1 ? "▤" : root.activeViewIndex === 2 ? "⌕" : root.activeViewIndex === 3 ? "◇" : "⚒"
                        color: root.theme.accentBright
                        font.pixelSize: 20
                    }
                }

                Text {
                    width: parent.width
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.appText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: "This surface is structurally ready and will be connected after the native Workbench is proven."
                    color: root.theme.mutedText
                    font.pixelSize: 10
                    lineHeight: 1.35
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
