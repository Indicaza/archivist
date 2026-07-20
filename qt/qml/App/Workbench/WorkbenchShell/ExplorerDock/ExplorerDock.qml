import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ExplorerItem"

Rectangle {
    id: root

    required property var theme
    required property int activeViewIndex

    signal closeRequested()

    property string selectedNodeId: ""
    property string filterText: ""
    property var expandedNodeIds: ({
        "backend": true,
        "frontend": true,
        "qt": true
    })

    readonly property var viewTitles: [
        "Library Explorer",
        "Archived Libraries",
        "Library Search",
        "Plugins",
        "Tools"
    ]

    readonly property var treeNodes: [
        { id: "backend", parentId: "", title: "backend", glyph: "□", folder: true },
        { id: "backend-data", parentId: "backend", title: "data", glyph: "□", folder: true },
        { id: "backend-src", parentId: "backend", title: "src", glyph: "□", folder: true },
        { id: "backend-package", parentId: "backend", title: "package.json", glyph: "{}", folder: false },
        { id: "backend-tsconfig", parentId: "backend", title: "tsconfig.json", glyph: "{}", folder: false },
        { id: "backend-data-cache", parentId: "backend-data", title: "ai-model-cache.json", glyph: "{}", folder: false },
        { id: "backend-api", parentId: "backend-src", title: "api", glyph: "□", folder: true },
        { id: "backend-core", parentId: "backend-src", title: "core", glyph: "□", folder: true },
        { id: "backend-database", parentId: "backend-src", title: "database", glyph: "□", folder: true },
        { id: "backend-app", parentId: "backend-src", title: "app.ts", glyph: "{}", folder: false },
        { id: "backend-chats", parentId: "backend-api", title: "chats", glyph: "□", folder: true },
        { id: "backend-libraries", parentId: "backend-api", title: "libraries", glyph: "□", folder: true },
        { id: "backend-agents", parentId: "backend-api", title: "agents", glyph: "□", folder: true },
        { id: "frontend", parentId: "", title: "frontend", glyph: "□", folder: true },
        { id: "frontend-electron", parentId: "frontend", title: "electron", glyph: "□", folder: true },
        { id: "frontend-src", parentId: "frontend", title: "src", glyph: "□", folder: true },
        { id: "frontend-package", parentId: "frontend", title: "package.json", glyph: "{}", folder: false },
        { id: "frontend-vite", parentId: "frontend", title: "vite.config.ts", glyph: "{}", folder: false },
        { id: "frontend-main-cjs", parentId: "frontend-electron", title: "main.cjs", glyph: "{}", folder: false },
        { id: "frontend-preload-cjs", parentId: "frontend-electron", title: "preload.cjs", glyph: "{}", folder: false },
        { id: "frontend-components", parentId: "frontend-src", title: "components", glyph: "□", folder: true },
        { id: "frontend-domains", parentId: "frontend-src", title: "domains", glyph: "□", folder: true },
        { id: "frontend-styles", parentId: "frontend-src", title: "styles", glyph: "□", folder: true },
        { id: "frontend-app", parentId: "frontend-src", title: "App.tsx", glyph: "{}", folder: false },
        { id: "frontend-workbench", parentId: "frontend-components", title: "workbench", glyph: "□", folder: true },
        { id: "frontend-chat", parentId: "frontend-components", title: "chat", glyph: "□", folder: true },
        { id: "qt", parentId: "", title: "qt", glyph: "□", folder: true },
        { id: "qt-qml", parentId: "qt", title: "qml", glyph: "□", folder: true },
        { id: "qt-src", parentId: "qt", title: "src", glyph: "□", folder: true },
        { id: "qt-cmake", parentId: "qt", title: "CMakeLists.txt", glyph: "▤", folder: false },
        { id: "qt-app", parentId: "qt-qml", title: "App", glyph: "□", folder: true },
        { id: "qt-app-qml", parentId: "qt-qml", title: "App.qml", glyph: "◇", folder: false },
        { id: "qt-main", parentId: "qt-src", title: "main.cpp", glyph: "{}", folder: false },
        { id: "catalog-test", parentId: "", title: "catalog-test.txt", glyph: "▤", folder: false, muted: true, warning: true },
        { id: "package-lock", parentId: "", title: "package-lock.json", glyph: "{}", folder: false },
        { id: "package-root", parentId: "", title: "package.json", glyph: "{}", folder: false },
        { id: "readme-root", parentId: "", title: "README.md", glyph: "▤", folder: false }
    ]

    color: theme.surfaceBg
    border.width: 0
    clip: true

    function isExpanded(nodeId) {
        return expandedNodeIds[nodeId] === true
    }

    function nodeMatches(node, query) {
        return query.length === 0
            || node.title.toLowerCase().indexOf(query) !== -1
    }

    function subtreeMatches(nodeId, query) {
        for (var index = 0; index < treeNodes.length; index += 1) {
            var child = treeNodes[index]

            if (child.parentId !== nodeId) {
                continue
            }

            if (nodeMatches(child, query) || subtreeMatches(child.id, query)) {
                return true
            }
        }

        return false
    }

    function appendVisibleChildren(parentId, depth, query) {
        for (var index = 0; index < treeNodes.length; index += 1) {
            var node = treeNodes[index]

            if (node.parentId !== parentId) {
                continue
            }

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
        appendVisibleChildren("", 0, filterText.trim().toLowerCase())
    }

    function activateNode(nodeId, folder) {
        selectedNodeId = nodeId

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

    Component.onCompleted: rebuildTree()

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

                        Text {
                            Layout.fillWidth: true
                            text: "Archivist"
                            color: root.theme.appText
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
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
                            text: "+"
                            hoverEnabled: true
                            padding: 0

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
                            text: "Archivist"
                            color: root.theme.mutedText
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            Layout.preferredWidth: 25
                            Layout.preferredHeight: 16
                            radius: 8
                            color: "#26231e"

                            Text {
                                anchors.centerIn: parent
                                text: String(root.treeNodes.length)
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
                            text: "↻"
                            hoverEnabled: true
                            padding: 0
                            onClicked: root.rebuildTree()

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

                ListView {
                    id: libraryList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 7
                    Layout.rightMargin: 7
                    Layout.topMargin: 3
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
                            text: root.selectedNodeId.length > 0
                                ? "Selected  ·  " + root.selectedNodeId
                                : "Ready  ·  Native tree prototype"
                            color: root.theme.mutedText
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
