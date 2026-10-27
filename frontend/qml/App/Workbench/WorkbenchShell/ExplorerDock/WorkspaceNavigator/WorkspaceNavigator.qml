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

    readonly property var collectionOptions: buildCollectionOptions()
    readonly property var scopedLibraries: filteredLibraries()
    readonly property real minimumLibraryHeight: 180

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

    Component.onCompleted: CollectionStore.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
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
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 3

                ComboBox {
                    id: collectionSelector

                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    model: root.collectionOptions
                    textRole: "path"
                    valueRole: "id"
                    enabled: !CollectionStore.loading
                        && !CollectionStore.mutating
                        && count > 0
                    hoverEnabled: true
                    leftPadding: 8
                    rightPadding: 22

                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.text: "Collections"
                    ToolTip.delay: 3000
                    ToolTip.timeout: 2400

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
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }

                    indicator: AppIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-down"
                        tone: collectionSelector.popup.visible
                            ? "accent"
                            : "muted"
                        iconSize: 14
                        accessibleLabel: "Choose Collection"
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
                        height: 28
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

                IconButton {
                    id: createCollectionButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    theme: root.theme
                    circular: true
                    idleBackgroundColor: root.theme.surfaceBg
                    hoverBackgroundColor: root.theme.hoverBg
                    iconName: "add"
                    iconSize: 15
                    enabled: !CollectionStore.mutating
                    toolTipText: "Create Collection"
                    onClicked: collectionEditor.openForCreate()
                }

                IconButton {
                    id: manageCollectionButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    theme: root.theme
                    circular: true
                    idleBackgroundColor: root.theme.surfaceBg
                    hoverBackgroundColor: root.theme.hoverBg
                    iconName: "more"
                    iconSize: 15
                    visible: CollectionStore.selectedCollectionId.length > 0
                    enabled: !CollectionStore.mutating
                    toolTipText: "Manage Collection"
                    onClicked: collectionEditor.openForEdit(
                        CollectionStore.selectedCollection
                    )
                }

                IconButton {
                    id: closeExplorerButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    theme: root.theme
                    circular: true
                    idleBackgroundColor: root.theme.surfaceBg
                    hoverBackgroundColor: root.theme.hoverBg
                    iconName: "chevron-left"
                    iconSize: 15
                    toolTipText: "Close Explorer"
                    onClicked: root.closeRequested()
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
    }

    CollectionEditor {
        id: collectionEditor
        theme: root.theme
    }
}
