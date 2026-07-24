import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Popup {
    id: root

    required property var theme
    property var attachedAgentIds: []
    property string chatTitle: ""

    readonly property var availableAgents: filteredAgents()

    signal agentSelected(string agentId)
    signal createRequested()

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(520, parent ? parent.width - 48 : 520)
    height: Math.min(520, parent ? parent.height - 48 : 520)
    padding: 0
    modal: true
    focus: true
    closePolicy: ChatStore.assigningAgent || AgentStore.mutating
        ? Popup.NoAutoClose
        : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function containsAttachedAgent(agentId) {
        var ids = attachedAgentIds || []

        for (var index = 0; index < ids.length; index += 1) {
            if (String(ids[index]) === String(agentId || "")) {
                return true
            }
        }

        return false
    }

    function filteredAgents() {
        var agents = AgentStore.agents || []
        var query = searchField.text.trim().toLowerCase()
        var available = []

        for (var index = 0; index < agents.length; index += 1) {
            var agent = agents[index]
            var agentId = String(agent.id || "")
            var haystack = (
                String(agent.name || "")
                + " "
                + String(agent.description || "")
            ).toLowerCase()

            if (
                agentId.length > 0
                && !containsAttachedAgent(agentId)
                && (query.length === 0 || haystack.indexOf(query) !== -1)
            ) {
                available.push(agent)
            }
        }

        return available
    }

    function openPicker() {
        searchField.clear()
        open()
        searchField.forceActiveFocus()
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: root.theme.motionModalLaunch
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                property: "scale"
                from: root.theme.modalSpawnScale
                to: 1
                duration: root.theme.motionModalLaunch
                easing.type: Easing.OutBack
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: root.theme.motionModalClose
                easing.type: Easing.InCubic
            }

            NumberAnimation {
                property: "scale"
                from: 1
                to: root.theme.modalSpawnScale
                duration: root.theme.motionModalClose
                easing.type: Easing.InCubic
            }
        }
    }

    Overlay.modal: Rectangle {
        color: "#aa090908"
        opacity: root.opacity
    }

    background: Rectangle {
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        radius: root.theme.radiusPanel
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            color: "#1a1916"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
            }

            Column {
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.leftMargin: 18
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Text {
                    text: "CHAT ROSTER"
                    color: root.theme.accentBright
                    font.pixelSize: root.theme.typeSize(8)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }

                Text {
                    width: parent.width
                    text: root.chatTitle.length > 0
                        ? "Add an Agent to " + root.chatTitle
                        : "Add an Agent"
                    color: root.theme.appText
                    font.family: root.theme.titleFontFamily
                    font.pixelSize: root.theme.typeSize(17)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Button {
                id: closeButton

                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                text: "×"
                hoverEnabled: true
                padding: 0
                onClicked: root.close()

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(17)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.hovered ? root.theme.hoverBg : "transparent"
                    radius: 4
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 14
            Layout.bottomMargin: 14
            spacing: 10

            TextField {
                id: searchField

                Layout.fillWidth: true
                Layout.preferredHeight: 36
                placeholderText: "Search existing Agents"
                placeholderTextColor: root.theme.composerPlaceholder
                color: root.theme.appText
                selectionColor: root.theme.messageSelectionBg
                selectedTextColor: root.theme.messageSelectionText
                font.pixelSize: root.theme.typeSize(10)
                leftPadding: 11
                rightPadding: 11

                background: Rectangle {
                    color: searchField.activeFocus
                        ? "#211f1b"
                        : root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: searchField.activeFocus
                        ? "#5a4d8c"
                        : root.theme.quietBorder
                    radius: 5
                }
            }

            ListView {
                id: agentList

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                model: root.availableAgents

                delegate: Rectangle {
                    id: agentRow

                    required property var modelData

                    width: agentList.width
                    height: 58
                    color: agentHover.hovered
                        ? root.theme.hoverBg
                        : root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: agentHover.hovered
                        ? "#554a7b"
                        : root.theme.quietBorder
                    radius: root.theme.radiusSmall

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 8
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            radius: 15
                            color: root.theme.activeBg
                            border.width: 1
                            border.color: root.theme.quietBorder

                            Text {
                                anchors.centerIn: parent
                                text: String(agentRow.modelData.name || "A")
                                    .slice(0, 1)
                                    .toUpperCase()
                                color: root.theme.accentBright
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.Bold
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                width: parent.width
                                text: String(
                                    agentRow.modelData.name || "Unnamed Agent"
                                )
                                color: root.theme.appText
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: String(
                                    agentRow.modelData.description
                                    || "Reusable Archivist Agent"
                                )
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(8)
                                opacity: 0.72
                                elide: Text.ElideRight
                            }
                        }

                        Button {
                            Layout.preferredWidth: 52
                            Layout.preferredHeight: 28
                            text: ChatStore.assigningAgent ? "…" : "Add"
                            enabled: !ChatStore.assigningAgent
                                && !AgentStore.mutating
                            hoverEnabled: true
                            padding: 0
                            onClicked: {
                                root.agentSelected(
                                    String(agentRow.modelData.id)
                                )
                                root.close()
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled && parent.hovered
                                    ? root.theme.appText
                                    : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(9)
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: parent.hovered
                                    ? "#2d2924"
                                    : root.theme.surfaceBg
                                border.width: 1
                                border.color: parent.hovered
                                    ? "#6557a0"
                                    : root.theme.panelBorder
                                radius: 4
                            }
                        }
                    }

                    HoverHandler {
                        id: agentHover
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.availableAgents.length === 0
                text: searchField.text.trim().length > 0
                    ? "No matching unattached Agents."
                    : "Every active Agent is already attached."
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(9)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                text: "+  Create a new Agent"
                enabled: !AgentStore.mutating && !ChatStore.assigningAgent
                hoverEnabled: true
                padding: 0
                onClicked: {
                    root.close()
                    root.createRequested()
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled && parent.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.hovered
                        ? root.theme.hoverBg
                        : root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: parent.hovered
                        ? "#6557a0"
                        : root.theme.quietBorder
                    radius: 5
                }
            }
        }
    }
}
