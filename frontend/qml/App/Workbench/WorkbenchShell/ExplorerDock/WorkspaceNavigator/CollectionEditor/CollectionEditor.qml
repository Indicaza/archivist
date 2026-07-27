import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Popup {
    id: editorRoot

    required property var theme
    property bool editing: false
    property var editingCollection: ({})
    property var selectedLibraryIds: []
    property var selectedChatIds: []
    property var selectedAgentIds: []
    property string defaultAgentId: ""
    property bool confirmingArchive: false

    readonly property bool busy: CollectionStore.mutating
    readonly property bool canSave: !busy
        && nameField.text.trim().length > 0
        && nameField.text.trim().length <= 120

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(860, parent ? parent.width - 48 : 860)
    height: Math.min(760, parent ? parent.height - 48 : 760)
    padding: 0
    modal: true
    focus: true
    closePolicy: busy
        ? Popup.NoAutoClose
        : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: editorRoot.theme.motionModalLaunch
                easing.type: Easing.OutCubic
            }

            SequentialAnimation {
                NumberAnimation {
                    property: "scale"
                    from: editorRoot.theme.modalSpawnScale
                    to: editorRoot.theme.modalOvershootScale
                    duration: editorRoot.theme.motionModalLaunch
                    easing.type: Easing.OutCubic
                }

                NumberAnimation {
                    property: "scale"
                    from: editorRoot.theme.modalOvershootScale
                    to: 1.0
                    duration: editorRoot.theme.motionModalSettle
                    easing.type: Easing.OutQuad
                }
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: editorRoot.theme.motionModalCloseKick
                    + editorRoot.theme.motionModalClose
                easing.type: Easing.InCubic
            }

            SequentialAnimation {
                NumberAnimation {
                    property: "scale"
                    from: 1.0
                    to: editorRoot.theme.modalOvershootScale
                    duration: editorRoot.theme.motionModalCloseKick
                    easing.type: Easing.OutQuad
                }

                NumberAnimation {
                    property: "scale"
                    from: editorRoot.theme.modalOvershootScale
                    to: editorRoot.theme.modalSpawnScale
                    duration: editorRoot.theme.motionModalClose
                    easing.type: Easing.InCubic
                }
            }
        }
    }

    Overlay.modal: Rectangle {
        color: "#aa090908"
        opacity: editorRoot.opacity
    }

    background: Rectangle {
        color: editorRoot.theme.surfaceBg
        border.width: 1
        border.color: editorRoot.theme.panelBorder
        radius: editorRoot.theme.radiusPanel
    }

    function copyIds(values) {
        var result = []
        var source = values || []

        for (var index = 0; index < source.length; index += 1) {
            result.push(String(source[index]))
        }

        return result
    }

    function containsId(values, id) {
        return copyIds(values).indexOf(String(id || "")) >= 0
    }

    function toggledIds(values, id, checked) {
        var result = copyIds(values)
        var normalizedId = String(id || "")
        var index = result.indexOf(normalizedId)

        if (checked && index < 0) {
            result.push(normalizedId)
        } else if (!checked && index >= 0) {
            result.splice(index, 1)
        }

        return result
    }

    function parentOptions() {
        var options = [{ id: "", path: "Top level" }]
        var collections = CollectionStore.collections || []
        var editingId = String(editingCollection.id || "")
        var editingPath = String(editingCollection.path || "")

        for (var index = 0; index < collections.length; index += 1) {
            var collection = collections[index]
            var collectionId = String(collection.id || "")
            var collectionPath = String(collection.path || collection.name || "Collection")
            var isDescendant = editingPath.length > 0
                && collectionPath.indexOf(editingPath + " / ") === 0

            if (collectionId !== editingId && !isDescendant) {
                options.push({
                    id: collectionId,
                    path: collectionPath
                })
            }
        }

        return options
    }

    function parentIndexForId(parentCollectionId) {
        var options = parentOptions()

        for (var index = 0; index < options.length; index += 1) {
            if (String(options[index].id) === String(parentCollectionId || "")) {
                return index
            }
        }

        return 0
    }

    function defaultAgentOptions() {
        var options = [{ id: "", name: "No default Agent" }]
        var agents = AgentStore.agents || []

        for (var index = 0; index < agents.length; index += 1) {
            var agent = agents[index]
            if (containsId(selectedAgentIds, agent.id)) {
                options.push({
                    id: String(agent.id),
                    name: String(agent.name || "Unnamed Agent")
                })
            }
        }

        return options
    }

    function openForCreate() {
        editing = false
        editingCollection = ({})
        selectedLibraryIds = []
        selectedChatIds = []
        selectedAgentIds = []
        defaultAgentId = ""
        confirmingArchive = false
        nameField.text = "New Collection"
        parentField.model = parentOptions()
        parentField.currentIndex = 0
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function openForEdit(collection) {
        if (!collection || !collection.id) {
            return
        }

        editing = true
        editingCollection = collection
        selectedLibraryIds = copyIds(collection.libraryIds)
        selectedChatIds = copyIds(collection.chatIds)
        selectedAgentIds = copyIds(collection.agentIds)
        defaultAgentId = String(collection.defaultAgentId || "")
        confirmingArchive = false
        nameField.text = String(collection.name || "")
        parentField.model = parentOptions()
        parentField.currentIndex = parentIndexForId(collection.parentCollectionId)
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function save() {
        if (!canSave) {
            return
        }

        var parentId = String(parentField.currentValue || "")

        if (editing) {
            CollectionStore.updateCollection(
                String(editingCollection.id),
                nameField.text.trim(),
                parentId,
                selectedLibraryIds,
                selectedChatIds,
                selectedAgentIds,
                defaultAgentId
            )
        } else {
            CollectionStore.createCollection(
                nameField.text.trim(),
                parentId,
                selectedLibraryIds,
                selectedChatIds,
                selectedAgentIds,
                defaultAgentId
            )
        }
    }

    Connections {
        target: CollectionStore

        function onCollectionCreated(collection) {
            if (editorRoot.visible && !editorRoot.editing) {
                editorRoot.close()
            }
        }

        function onCollectionUpdated(collection) {
            if (
                editorRoot.visible
                && editorRoot.editing
                && String(collection.id)
                    === String(editorRoot.editingCollection.id)
            ) {
                editorRoot.close()
            }
        }

        function onCollectionArchived(collection) {
            if (
                editorRoot.visible
                && String(collection.id)
                    === String(editorRoot.editingCollection.id)
            ) {
                editorRoot.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        CollectionEditorHeader {
            theme: editorRoot.theme
            editing: editorRoot.editing
            busy: editorRoot.busy
            collectionPath: String(
                editorRoot.editingCollection.path || "Collection"
            )
            onCloseRequested: editorRoot.close()
        }

        ScrollView {
            id: editorScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: editorScroll.availableWidth
                spacing: 16

                Item {
                    Layout.preferredHeight: 2
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5

                        Text {
                            text: "Collection name"
                            color: editorRoot.theme.mutedText
                            font.pixelSize: editorRoot.theme.typeSize(9)
                            font.weight: Font.DemiBold
                        }

                        TextField {
                            id: nameField

                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            enabled: !editorRoot.busy
                            maximumLength: 120
                            color: editorRoot.theme.appText
                            selectionColor: editorRoot.theme.messageSelectionBg
                            selectedTextColor: editorRoot.theme.messageSelectionText
                            font.pixelSize: editorRoot.theme.typeSize(12)
                            leftPadding: 11
                            rightPadding: 11
                            onTextChanged: editorRoot.confirmingArchive = false

                            background: Rectangle {
                                color: nameField.activeFocus
                                    ? "#211f1b"
                                    : editorRoot.theme.controlSurfaceBg
                                border.width: 1
                                border.color: nameField.activeFocus
                                    ? "#5a4d8c"
                                    : editorRoot.theme.quietBorder
                                radius: 5
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 270
                        spacing: 5

                        Text {
                            text: "Parent Collection"
                            color: editorRoot.theme.mutedText
                            font.pixelSize: editorRoot.theme.typeSize(9)
                            font.weight: Font.DemiBold
                        }

                        ComboBox {
                            id: parentField

                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            enabled: !editorRoot.busy
                            textRole: "path"
                            valueRole: "id"
                            font.pixelSize: editorRoot.theme.typeSize(11)

                            background: Rectangle {
                                color: editorRoot.theme.controlSurfaceBg
                                border.width: 1
                                border.color: editorRoot.theme.quietBorder
                                radius: 5
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    text: "Collections are references. Removing one never moves files or deletes Chats."
                    color: editorRoot.theme.mutedText
                    font.pixelSize: editorRoot.theme.typeSize(9)
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.preferredHeight: 1
                    color: editorRoot.theme.quietBorder
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    columns: editorRoot.width >= 760 ? 3 : 1
                    columnSpacing: 12
                    rowSpacing: 12

                    CollectionMembershipCard {
                        theme: editorRoot.theme
                        heading: "LIBRARIES"
                        description: "Knowledge roots visible in this Collection."
                        items: LibraryStore.libraries
                        selectedIds: editorRoot.selectedLibraryIds
                        itemLabel: function(item) {
                            return String(item.name || "Library")
                        }
                        busy: editorRoot.busy
                        onItemToggled: function(itemId, checked) {
                            editorRoot.selectedLibraryIds =
                                editorRoot.toggledIds(
                                    editorRoot.selectedLibraryIds,
                                    itemId,
                                    checked
                                )
                        }
                    }

                    CollectionMembershipCard {
                        theme: editorRoot.theme
                        heading: "PINNED CHATS"
                        description: "Curated Chats. Library Chats appear automatically."
                        items: ChatStore.chats
                        selectedIds: editorRoot.selectedChatIds
                        itemLabel: function(item) {
                            return String(item.title || "Untitled Chat")
                        }
                        busy: editorRoot.busy
                        onItemToggled: function(itemId, checked) {
                            editorRoot.selectedChatIds =
                                editorRoot.toggledIds(
                                    editorRoot.selectedChatIds,
                                    itemId,
                                    checked
                                )
                        }
                    }

                    CollectionMembershipCard {
                        theme: editorRoot.theme
                        heading: "AGENT ROSTER"
                        description: "Preferred workers appear first; Agents remain global."
                        items: AgentStore.agents
                        selectedIds: editorRoot.selectedAgentIds
                        itemLabel: function(item) {
                            return String(item.name || "Unnamed Agent")
                        }
                        busy: editorRoot.busy
                        showDefaultAgent: true
                        defaultAgentOptions: editorRoot.defaultAgentOptions()
                        defaultAgentId: editorRoot.defaultAgentId
                        onItemToggled: function(itemId, checked) {
                            editorRoot.selectedAgentIds =
                                editorRoot.toggledIds(
                                    editorRoot.selectedAgentIds,
                                    itemId,
                                    checked
                                )

                            if (
                                !checked
                                && editorRoot.defaultAgentId === itemId
                            ) {
                                editorRoot.defaultAgentId = ""
                            }
                        }
                        onDefaultAgentSelected: function(agentId) {
                            editorRoot.defaultAgentId = agentId
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    visible: CollectionStore.errorMessage.length > 0
                    text: CollectionStore.errorMessage
                    color: editorRoot.theme.danger
                    font.pixelSize: editorRoot.theme.typeSize(9)
                    wrapMode: Text.Wrap
                }

                ArchivedCollectionsSection {
                    theme: editorRoot.theme
                    busy: editorRoot.busy
                }

                Item {
                    Layout.preferredHeight: 4
                }
            }
        }

        CollectionEditorFooter {
            theme: editorRoot.theme
            editing: editorRoot.editing
            busy: editorRoot.busy
            canSave: editorRoot.canSave
            confirmingArchive: editorRoot.confirmingArchive
            onArchiveRequested: {
                if (!editorRoot.confirmingArchive) {
                    editorRoot.confirmingArchive = true
                    return
                }

                CollectionStore.archiveCollection(
                    String(editorRoot.editingCollection.id)
                )
            }
            onCancelRequested: editorRoot.close()
            onSaveRequested: editorRoot.save()
        }
    }
}
