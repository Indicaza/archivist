import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "CollectionEditor"
import "."

Item {
    id: root

    required property var theme
    required property bool expanded

    property int hoveredIndex: -1

    readonly property var rows: buildRows()

    signal toggleRequested()
    signal expandRequested()

    function buildRows() {
        var result = [{
            id: "",
            name: "All Work",
            path: "All Work",
            libraryIds: [],
            chatIds: []
        }]
        var collections = CollectionStore.collections || []

        for (var index = 0; index < collections.length; index += 1) {
            result.push(collections[index])
        }

        return result
    }

    function collectionDepth(collection) {
        var path = String(collection.path || collection.name || "")

        if (path.length === 0 || path === "All Work") {
            return 0
        }

        return Math.max(0, path.split(" / ").length - 1)
    }

    Component.onCompleted: CollectionStore.refresh()

    Connections {
        target: CollectionStore

        function onCollectionCreated(collection) {
            root.expandRequested()
        }
    }

    SidebarBandHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 36
        theme: root.theme
        title: "COLLECTIONS"
        glyph: "▦"
        count: CollectionStore.loading
            ? 0
            : CollectionStore.collections.length
        expanded: root.expanded
        primaryVisible: true
        primaryEnabled: !CollectionStore.mutating
        primaryToolTip: "Create Collection"
        secondaryVisible: true
        secondaryEnabled: CollectionStore.selectedCollectionId.length > 0
            && !CollectionStore.mutating
        secondaryToolTip: "Manage selected Collection"
        onToggleRequested: root.toggleRequested()
        onPrimaryRequested: collectionEditor.openForCreate()
        onSecondaryRequested: collectionEditor.openForEdit(
            CollectionStore.selectedCollection
        )
    }

    ListView {
        id: collectionList

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        visible: root.expanded
        clip: true
        spacing: 1
        boundsBehavior: Flickable.StopAtBounds
        model: root.rows

        delegate: Item {
            id: collectionDelegate

            required property int index
            required property var modelData

            width: collectionList.width
            height: 37
            z: collectionHover.hovered
                ? 3
                : collectionDelegate.neighborHovered
                    ? 2
                    : 1

            readonly property bool selected: String(modelData.id || "")
                === String(CollectionStore.selectedCollectionId)
            readonly property bool neighborHovered: root.hoveredIndex >= 0
                && Math.abs(root.hoveredIndex - index) === 1
            readonly property int depth: root.collectionDepth(modelData)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                height: 31
                radius: root.theme.radiusSmall
                color: collectionTap.pressed
                    ? "#292621"
                    : collectionHover.hovered
                        ? root.theme.hoverBg
                        : collectionDelegate.selected
                            ? root.theme.activeBg
                            : "transparent"
                border.width: collectionDelegate.selected ? 1 : 0
                border.color: "#554a7b"
                scale: collectionTap.pressed
                    ? root.theme.pressedScale
                    : collectionHover.hovered
                        ? root.theme.hoverScale
                        : collectionDelegate.neighborHovered
                            ? root.theme.hoverNeighborScale
                            : 1.0

                Behavior on scale {
                    enabled: !collectionTap.pressed

                    NumberAnimation {
                        duration: collectionHover.hovered
                            || collectionDelegate.neighborHovered
                                ? root.theme.motionHover
                                : root.theme.motionHoverExit
                        easing.type: Easing.OutCubic
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10 + collectionDelegate.depth * 13
                    anchors.rightMargin: 9
                    spacing: 7

                    Text {
                        text: collectionDelegate.depth > 0 ? "└" : "▦"
                        color: collectionDelegate.selected
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(9)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: String(
                            collectionDelegate.modelData.name
                                || collectionDelegate.modelData.path
                                || "Collection"
                        )
                        color: collectionDelegate.selected
                            || collectionHover.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(10)
                        font.weight: collectionDelegate.selected
                            ? Font.DemiBold
                            : Font.Normal
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: String(
                            collectionDelegate.modelData.id || ""
                        ).length > 0
                        text: String(
                            (collectionDelegate.modelData.libraryIds || []).length
                        ) + " / " + String(
                            (collectionDelegate.modelData.chatIds || []).length
                        )
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(8)
                        opacity: 0.68
                    }
                }
            }

            HoverHandler {
                id: collectionHover

                onHoveredChanged: {
                    if (hovered) {
                        root.hoveredIndex = collectionDelegate.index
                    } else if (root.hoveredIndex === collectionDelegate.index) {
                        root.hoveredIndex = -1
                    }
                }
            }

            TapHandler {
                id: collectionTap

                enabled: !CollectionStore.loading
                    && !CollectionStore.mutating
                onTapped: CollectionStore.selectCollection(
                    String(collectionDelegate.modelData.id || "")
                )
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    CollectionEditor {
        id: collectionEditor

        theme: root.theme
    }
}
