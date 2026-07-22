import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Popup {
    id: editorRoot

    required property var theme
    property bool editing: false
    property var editingAgent: ({})
    property bool confirmingArchive: false
    property bool confirmingDelete: false

    readonly property bool archived: editing
        && Boolean(editingAgent && editingAgent.archivedAt)
    readonly property bool builtIn: editing
        && Boolean(editingAgent && editingAgent.isBuiltIn)
    readonly property bool formEnabled: !AgentStore.mutating && !archived

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(820, parent ? parent.width - 48 : 820)
    height: Math.min(760, parent ? parent.height - 48 : 760)
    padding: 0
    modal: true
    focus: true
    closePolicy: AgentStore.mutating
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

    component EditorTextField: ColumnLayout {
        id: field

        required property var theme
        required property string label
        property alias text: control.text
        property string placeholderText: ""
        property int maximumLength: 32767

        Layout.fillWidth: true
        spacing: 4

        Text {
            text: field.label
            color: field.theme.mutedText
            font.pixelSize: 9
            font.weight: Font.DemiBold
        }

        TextField {
            id: control

            Layout.fillWidth: true
            Layout.preferredHeight: 34
            enabled: field.enabled
            placeholderText: field.placeholderText
            placeholderTextColor: field.theme.composerPlaceholder
            color: field.theme.appText
            selectionColor: "#5a554b"
            selectedTextColor: "#ffffff"
            font.pixelSize: 11
            leftPadding: 10
            rightPadding: 10
            maximumLength: field.maximumLength

            background: Rectangle {
                color: control.activeFocus ? "#211f1b" : field.theme.controlSurfaceBg
                border.width: 1
                border.color: control.activeFocus
                    ? "#5a4d8c"
                    : field.theme.quietBorder
                radius: 4
            }
        }
    }

    component EditorTextArea: ColumnLayout {
        id: field

        required property var theme
        required property string label
        property alias text: control.text
        property string placeholderText: ""
        property int editorHeight: 72

        Layout.fillWidth: true
        spacing: 4

        Text {
            text: field.label
            color: field.theme.mutedText
            font.pixelSize: 9
            font.weight: Font.DemiBold
        }

        TextArea {
            id: control

            Layout.fillWidth: true
            Layout.preferredHeight: field.editorHeight
            enabled: field.enabled
            placeholderText: field.placeholderText
            placeholderTextColor: field.theme.composerPlaceholder
            color: field.theme.appText
            selectionColor: "#5a554b"
            selectedTextColor: "#ffffff"
            font.pixelSize: 11
            wrapMode: TextEdit.Wrap
            leftPadding: 10
            rightPadding: 10
            topPadding: 8
            bottomPadding: 8

            background: Rectangle {
                color: control.activeFocus ? "#211f1b" : field.theme.controlSurfaceBg
                border.width: 1
                border.color: control.activeFocus
                    ? "#5a4d8c"
                    : field.theme.quietBorder
                radius: 4
            }
        }
    }

    function stringValue(value) {
        return value === undefined || value === null ? "" : String(value)
    }

    function listText(value) {
        if (!value || value.length === undefined) {
            return ""
        }

        var items = []
        for (var index = 0; index < value.length; index += 1) {
            items.push(String(value[index]))
        }

        return items.join("\n")
    }

    function textList(value) {
        var lines = String(value || "").split("\n")
        var items = []

        for (var index = 0; index < lines.length; index += 1) {
            var item = lines[index].trim()
            if (item.length > 0) {
                items.push(item)
            }
        }

        return items
    }

    function nullableText(value) {
        var trimmed = String(value || "").trim()
        return trimmed.length > 0 ? trimmed : null
    }

    function resetConfirmations() {
        confirmingArchive = false
        confirmingDelete = false
    }

    function resetFields() {
        nameField.text = ""
        descriptionField.text = ""
        jobTitleField.text = ""
        personalityField.text = ""
        temperamentField.text = ""
        voiceField.text = ""
        backstoryField.text = ""
        missionField.text = ""
        doctrineField.text = ""
        expertiseField.text = ""
        responsibilitiesField.text = ""
        successCriteriaField.text = ""
        limitationsField.text = ""
        responseStyleField.text = ""
        verbosityField.currentIndex = 1
        formattingRulesField.text = ""
        codePreferencesField.text = ""
        citationField.text = ""
        followUpField.text = ""
        systemInstructionsField.text = ""
    }

    function openForCreate() {
        AgentStore.clearError()
        editing = false
        editingAgent = ({})
        resetConfirmations()
        resetFields()
        nameField.text = "New Agent"
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function openForEdit(agent) {
        AgentStore.clearError()
        editing = true
        editingAgent = agent || ({})
        resetConfirmations()

        var identity = editingAgent.identity || ({})
        var profession = editingAgent.profession || ({})
        var output = editingAgent.outputContract || ({})

        nameField.text = stringValue(editingAgent.name)
        descriptionField.text = stringValue(editingAgent.description)
        jobTitleField.text = stringValue(profession.jobTitle)
        personalityField.text = stringValue(identity.personality)
        temperamentField.text = stringValue(identity.temperament)
        voiceField.text = stringValue(identity.voice)
        backstoryField.text = stringValue(identity.backstory)
        missionField.text = stringValue(profession.mission)
        doctrineField.text = stringValue(editingAgent.doctrine)
        expertiseField.text = listText(profession.expertise)
        responsibilitiesField.text = listText(profession.responsibilities)
        successCriteriaField.text = listText(profession.successCriteria)
        limitationsField.text = listText(profession.limitations)
        responseStyleField.text = stringValue(output.responseStyle)

        var verbosity = stringValue(output.verbosity)
        verbosityField.currentIndex = Math.max(
            0,
            ["concise", "balanced", "detailed"].indexOf(verbosity)
        )

        formattingRulesField.text = listText(output.formattingRules)
        codePreferencesField.text = listText(output.codeOutputPreferences)
        citationField.text = stringValue(output.citationRequirements)
        followUpField.text = stringValue(output.followUpBehavior)
        systemInstructionsField.text = stringValue(editingAgent.systemInstructions)

        open()
    }

    function createPayload() {
        var input = {
            name: nameField.text.trim()
        }

        if (descriptionField.text.trim().length > 0) {
            input.description = descriptionField.text.trim()
        }

        var identity = ({})
        if (personalityField.text.trim().length > 0) {
            identity.personality = personalityField.text.trim()
        }
        if (temperamentField.text.trim().length > 0) {
            identity.temperament = temperamentField.text.trim()
        }
        if (voiceField.text.trim().length > 0) {
            identity.voice = voiceField.text.trim()
        }
        if (backstoryField.text.trim().length > 0) {
            identity.backstory = backstoryField.text.trim()
        }
        if (Object.keys(identity).length > 0) {
            input.identity = identity
        }

        var profession = ({})
        if (jobTitleField.text.trim().length > 0) {
            profession.jobTitle = jobTitleField.text.trim()
        }
        if (missionField.text.trim().length > 0) {
            profession.mission = missionField.text.trim()
        }

        var expertise = textList(expertiseField.text)
        var responsibilities = textList(responsibilitiesField.text)
        var successCriteria = textList(successCriteriaField.text)
        var limitations = textList(limitationsField.text)

        if (expertise.length > 0) {
            profession.expertise = expertise
        }
        if (responsibilities.length > 0) {
            profession.responsibilities = responsibilities
        }
        if (successCriteria.length > 0) {
            profession.successCriteria = successCriteria
        }
        if (limitations.length > 0) {
            profession.limitations = limitations
        }
        if (Object.keys(profession).length > 0) {
            input.profession = profession
        }

        if (doctrineField.text.trim().length > 0) {
            input.doctrine = doctrineField.text.trim()
        }

        var output = ({})
        if (responseStyleField.text.trim().length > 0) {
            output.responseStyle = responseStyleField.text.trim()
        }

        var selectedVerbosity = String(verbosityField.currentText || "balanced")
        if (selectedVerbosity !== "balanced") {
            output.verbosity = selectedVerbosity
        }

        var formattingRules = textList(formattingRulesField.text)
        var codePreferences = textList(codePreferencesField.text)

        if (formattingRules.length > 0) {
            output.formattingRules = formattingRules
        }
        if (codePreferences.length > 0) {
            output.codeOutputPreferences = codePreferences
        }
        if (citationField.text.trim().length > 0) {
            output.citationRequirements = citationField.text.trim()
        }
        if (followUpField.text.trim().length > 0) {
            output.followUpBehavior = followUpField.text.trim()
        }
        if (Object.keys(output).length > 0) {
            input.outputContract = output
        }

        if (systemInstructionsField.text.trim().length > 0) {
            input.systemInstructions = systemInstructionsField.text.trim()
        }

        return input
    }

    function updatePayload() {
        return {
            name: nameField.text.trim(),
            description: nullableText(descriptionField.text),
            identity: {
                personality: nullableText(personalityField.text),
                temperament: nullableText(temperamentField.text),
                voice: nullableText(voiceField.text),
                backstory: nullableText(backstoryField.text)
            },
            profession: {
                jobTitle: nullableText(jobTitleField.text),
                mission: nullableText(missionField.text),
                expertise: textList(expertiseField.text),
                responsibilities: textList(responsibilitiesField.text),
                successCriteria: textList(successCriteriaField.text),
                limitations: textList(limitationsField.text)
            },
            doctrine: nullableText(doctrineField.text),
            outputContract: {
                responseStyle: nullableText(responseStyleField.text),
                verbosity: String(verbosityField.currentText || "balanced"),
                formattingRules: textList(formattingRulesField.text),
                codeOutputPreferences: textList(codePreferencesField.text),
                citationRequirements: nullableText(citationField.text),
                followUpBehavior: nullableText(followUpField.text)
            },
            systemInstructions: nullableText(systemInstructionsField.text)
        }
    }

    function save() {
        if (
            AgentStore.mutating
            || archived
            || nameField.text.trim().length === 0
        ) {
            return
        }

        if (editing) {
            AgentStore.updateAgent(String(editingAgent.id), updatePayload())
            return
        }

        AgentStore.createAgent(createPayload())
    }

    function duplicateCurrent() {
        if (!editing || AgentStore.mutating) {
            return
        }

        resetConfirmations()
        AgentStore.duplicateAgent(String(editingAgent.id))
    }

    function archiveCurrent() {
        if (!editing || archived || builtIn || AgentStore.mutating) {
            return
        }

        if (!confirmingArchive) {
            confirmingArchive = true
            confirmingDelete = false
            return
        }

        AgentStore.archiveAgent(String(editingAgent.id))
    }

    function restoreCurrent() {
        if (!editing || !archived || AgentStore.mutating) {
            return
        }

        resetConfirmations()
        AgentStore.restoreAgent(String(editingAgent.id))
    }

    function deleteCurrent() {
        if (!editing || !archived || builtIn || AgentStore.mutating) {
            return
        }

        if (!confirmingDelete) {
            confirmingDelete = true
            confirmingArchive = false
            return
        }

        AgentStore.deleteAgent(String(editingAgent.id))
    }

    Connections {
        target: AgentStore

        function onAgentCreated(agent) {
            if (editorRoot.visible && !editorRoot.editing) {
                editorRoot.close()
            }
        }

        function onAgentUpdated(agent) {
            if (
                editorRoot.visible
                && editorRoot.editing
                && String(agent.id) === String(editorRoot.editingAgent.id)
            ) {
                editorRoot.close()
            }
        }

        function onAgentDuplicated(agent) {
            if (editorRoot.visible && editorRoot.editing) {
                editorRoot.openForEdit(agent)
            }
        }

        function onAgentArchived(agent) {
            if (
                editorRoot.visible
                && String(agent.id) === String(editorRoot.editingAgent.id)
            ) {
                editorRoot.close()
            }
        }

        function onAgentRestored(agent) {
            if (
                editorRoot.visible
                && String(agent.id) === String(editorRoot.editingAgent.id)
            ) {
                editorRoot.close()
            }
        }

        function onAgentDeleted(agentId, reassignedChatCount) {
            if (
                editorRoot.visible
                && String(agentId) === String(editorRoot.editingAgent.id)
            ) {
                editorRoot.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: "#1a1916"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: editorRoot.theme.quietBorder
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: lifecycleActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Text {
                    text: editorRoot.archived
                        ? "ARCHIVED AGENT"
                        : editorRoot.editing
                            ? "EDIT AGENT"
                            : "CREATE AGENT"
                    color: editorRoot.archived
                        ? editorRoot.theme.warning
                        : editorRoot.theme.accentBright
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }

                Text {
                    width: parent.width
                    text: editorRoot.editing
                        ? stringValue(editorRoot.editingAgent.name)
                        : "New Agent"
                    color: editorRoot.theme.appText
                    font.family: editorRoot.theme.titleFontFamily
                    font.pixelSize: 21
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Row {
                id: lifecycleActions

                anchors.right: closeButton.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Button {
                    width: 72
                    height: 30
                    visible: editorRoot.editing && !editorRoot.archived
                    text: AgentStore.mutating ? "Working…" : "Duplicate"
                    enabled: !AgentStore.mutating
                    hoverEnabled: true
                    onClicked: editorRoot.duplicateCurrent()

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? editorRoot.theme.appText
                            : editorRoot.theme.mutedText
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.theme.hoverBg
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    width: editorRoot.confirmingArchive ? 110 : 72
                    height: 30
                    visible: editorRoot.editing
                        && !editorRoot.archived
                        && !editorRoot.builtIn
                    text: AgentStore.mutating
                        ? "Working…"
                        : editorRoot.confirmingArchive
                            ? "Confirm Archive"
                            : "Archive"
                    enabled: !AgentStore.mutating
                    hoverEnabled: true
                    onClicked: editorRoot.archiveCurrent()

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.confirmingArchive
                            ? editorRoot.theme.warning
                            : editorRoot.theme.appText
                        font.pixelSize: 9
                        font.weight: editorRoot.confirmingArchive ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.theme.hoverBg
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.confirmingArchive
                            ? editorRoot.theme.warning
                            : editorRoot.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    width: 72
                    height: 30
                    visible: editorRoot.editing && editorRoot.archived
                    text: AgentStore.mutating ? "Working…" : "Restore"
                    enabled: !AgentStore.mutating
                    hoverEnabled: true
                    onClicked: editorRoot.restoreCurrent()

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.theme.appText
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.theme.hoverBg
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    width: editorRoot.confirmingDelete ? 104 : 66
                    height: 30
                    visible: editorRoot.editing
                        && editorRoot.archived
                        && !editorRoot.builtIn
                    text: AgentStore.mutating
                        ? "Working…"
                        : editorRoot.confirmingDelete
                            ? "Confirm Delete"
                            : "Delete"
                    enabled: !AgentStore.mutating
                    hoverEnabled: true
                    onClicked: editorRoot.deleteCurrent()

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.theme.danger
                        font.pixelSize: 9
                        font.weight: editorRoot.confirmingDelete ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? "#2b1b1b"
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.confirmingDelete
                            ? editorRoot.theme.danger
                            : editorRoot.theme.quietBorder
                        radius: 4
                    }
                }
            }

            Button {
                id: closeButton

                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                text: "×"
                enabled: !AgentStore.mutating
                hoverEnabled: true
                padding: 0
                onClicked: editorRoot.close()

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered
                        ? editorRoot.theme.appText
                        : editorRoot.theme.mutedText
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.hovered ? editorRoot.theme.hoverBg : "transparent"
                    radius: 4
                }
            }
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
                spacing: 12

                Item {
                    Layout.preferredHeight: 4
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    implicitHeight: identitySection.implicitHeight + 24
                    color: editorRoot.theme.controlSurfaceBg
                    border.width: 1
                    border.color: editorRoot.theme.quietBorder
                    radius: 6

                    ColumnLayout {
                        id: identitySection

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "IDENTITY"
                            color: editorRoot.theme.accentBright
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10

                            EditorTextField {
                                id: nameField
                                theme: editorRoot.theme
                                label: "Name"
                                maximumLength: 120
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextField {
                                id: jobTitleField
                                theme: editorRoot.theme
                                label: "Professional title"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: descriptionField
                                theme: editorRoot.theme
                                label: "Description"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: personalityField
                                theme: editorRoot.theme
                                label: "Personality"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: temperamentField
                                theme: editorRoot.theme
                                label: "Temperament"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: voiceField
                                theme: editorRoot.theme
                                label: "Voice"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: backstoryField
                                theme: editorRoot.theme
                                label: "Backstory"
                                editorHeight: 88
                                enabled: editorRoot.formEnabled
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    implicitHeight: professionSection.implicitHeight + 24
                    color: editorRoot.theme.controlSurfaceBg
                    border.width: 1
                    border.color: editorRoot.theme.quietBorder
                    radius: 6

                    ColumnLayout {
                        id: professionSection

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "PROFESSION AND DOCTRINE"
                            color: editorRoot.theme.accentBright
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        EditorTextArea {
                            id: missionField
                            theme: editorRoot.theme
                            label: "Mission"
                            editorHeight: 82
                            enabled: editorRoot.formEnabled
                        }

                        EditorTextArea {
                            id: doctrineField
                            theme: editorRoot.theme
                            label: "Working doctrine"
                            editorHeight: 110
                            enabled: editorRoot.formEnabled
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10

                            EditorTextArea {
                                id: expertiseField
                                theme: editorRoot.theme
                                label: "Expertise — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: responsibilitiesField
                                theme: editorRoot.theme
                                label: "Responsibilities — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: successCriteriaField
                                theme: editorRoot.theme
                                label: "Success criteria — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: limitationsField
                                theme: editorRoot.theme
                                label: "Limitations — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    implicitHeight: outputSection.implicitHeight + 24
                    color: editorRoot.theme.controlSurfaceBg
                    border.width: 1
                    border.color: editorRoot.theme.quietBorder
                    radius: 6

                    ColumnLayout {
                        id: outputSection

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "OUTPUT CONTRACT"
                            color: editorRoot.theme.accentBright
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10

                            EditorTextArea {
                                id: responseStyleField
                                theme: editorRoot.theme
                                label: "Response style"
                                enabled: editorRoot.formEnabled
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: "Verbosity"
                                    color: editorRoot.theme.mutedText
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                ComboBox {
                                    id: verbosityField

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    enabled: editorRoot.formEnabled
                                    model: ["concise", "balanced", "detailed"]
                                    currentIndex: 1

                                    contentItem: Text {
                                        leftPadding: 10
                                        text: parent.displayText
                                        color: editorRoot.theme.appText
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    background: Rectangle {
                                        color: editorRoot.theme.controlSurfaceBg
                                        border.width: 1
                                        border.color: parent.activeFocus
                                            ? "#5a4d8c"
                                            : editorRoot.theme.quietBorder
                                        radius: 4
                                    }
                                }
                            }

                            EditorTextArea {
                                id: formattingRulesField
                                theme: editorRoot.theme
                                label: "Formatting rules — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: codePreferencesField
                                theme: editorRoot.theme
                                label: "Code preferences — one item per line"
                                editorHeight: 96
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: citationField
                                theme: editorRoot.theme
                                label: "Citation requirements"
                                enabled: editorRoot.formEnabled
                            }

                            EditorTextArea {
                                id: followUpField
                                theme: editorRoot.theme
                                label: "Follow-up behavior"
                                enabled: editorRoot.formEnabled
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    implicitHeight: instructionSection.implicitHeight + 24
                    color: editorRoot.theme.controlSurfaceBg
                    border.width: 1
                    border.color: editorRoot.theme.quietBorder
                    radius: 6

                    ColumnLayout {
                        id: instructionSection

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "SYSTEM INSTRUCTIONS"
                            color: editorRoot.theme.accentBright
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        EditorTextArea {
                            id: systemInstructionsField
                            theme: editorRoot.theme
                            label: "Explicit instructions applied after the structured profile"
                            editorHeight: 150
                            enabled: editorRoot.formEnabled
                        }

                        Text {
                            Layout.fillWidth: true
                            text: editorRoot.archived
                                ? "Archived Agent profiles are read-only until restored."
                                : editorRoot.editing
                                    ? "Generation model and Context Compiler settings are preserved unchanged in this slice."
                                    : "New Agents use the backend's default generation and Context Compiler settings."
                            color: editorRoot.theme.mutedText
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                            opacity: 0.75
                        }
                    }
                }

                Item {
                    Layout.preferredHeight: 4
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: "#171613"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: editorRoot.theme.quietBorder
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 240
                visible: AgentStore.errorMessage.length > 0
                text: AgentStore.errorMessage
                color: editorRoot.theme.danger
                font.pixelSize: 9
                elide: Text.ElideRight
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Button {
                    width: 74
                    height: 32
                    text: "Cancel"
                    enabled: !AgentStore.mutating
                    hoverEnabled: true
                    onClicked: editorRoot.close()

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.theme.appText
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.theme.hoverBg
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    width: 94
                    height: 32
                    visible: !editorRoot.archived
                    text: AgentStore.mutating
                        ? "Saving…"
                        : editorRoot.editing
                            ? "Save Agent"
                            : "Create Agent"
                    enabled: editorRoot.formEnabled
                        && nameField.text.trim().length > 0
                    hoverEnabled: true
                    onClicked: editorRoot.save()

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? editorRoot.theme.appText
                            : editorRoot.theme.mutedText
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.enabled
                            ? parent.hovered
                                ? "#554a7b"
                                : "#463d68"
                            : "#24211d"
                        border.width: 1
                        border.color: parent.enabled
                            ? "#6a5c99"
                            : editorRoot.theme.quietBorder
                        radius: 4
                    }
                }
            }
        }
    }
}
