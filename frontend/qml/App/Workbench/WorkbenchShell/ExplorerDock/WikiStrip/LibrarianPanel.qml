import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

// The Library's librarians: turn them on, allow OpenAI research, choose when
// they work, start or stop a run, and review what they found and proposed.
//
// Librarians only propose. Approving a proposal here is the same as
// approving it in the Wiki window; nothing touches the Library's files.
Dialog {
    id: panel

    required property var theme
    required property string libraryId

    objectName: "librarianPanel"
    property string message: ""
    readonly property var status: {
        Wiki.revision
        return panel.opened && panel.libraryId.length > 0 ? Wiki.librarianStatus(panel.libraryId) : ({})
    }
    readonly property var settings: panel.status.settings || ({})
    readonly property var today: (panel.status.usage || {}).today || ({})
    readonly property var month: (panel.status.usage || {}).last30Days || ({})
    readonly property var findingList: {
        Wiki.revision
        return panel.opened ? Wiki.librarianFindings(panel.libraryId) : []
    }
    readonly property var proposalList: {
        Wiki.revision
        return panel.opened ? Wiki.librarianProposals(panel.libraryId) : []
    }
    readonly property bool enabledHere: panel.settings.enabled === true
    readonly property bool consented: panel.settings.consented === true
    readonly property bool paused: panel.settings.paused === true

    function report(result, success) {
        panel.message = result && result.ok === false ? String(result.message || "That did not work.") : (success || "")
    }

    function runLabel(run) {
        let text = run.librarian + " · " + run.trigger + " · " + run.status
        if (Number(run.modelCalls || 0) > 0)
            text += " · " + run.modelCalls + (run.modelCalls === 1 ? " call" : " calls") + ", "
                + Number((run.inputTokens || 0) + (run.outputTokens || 0)).toLocaleString(Qt.locale(), "f", 0) + " tokens"
        if (Number(run.findingsCreated || 0) > 0)
            text += " · " + run.findingsCreated + " found"
        if (Number(run.proposalsCreated || 0) > 0)
            text += " · " + run.proposalsCreated + " proposed"
        if (run.activeJobs > 0)
            text += " · " + run.activeJobs + " working"
        return text
    }

    function kindLabel(kind) {
        switch (String(kind)) {
        case "stale-citation": return "Stale source"
        case "unsupported-claim": return "Unsupported"
        case "contradiction": return "Contradiction"
        case "suspicious-assumption": return "Assumption"
        case "coverage-gap": return "Coverage gap"
        case "deprecation-candidate": return "Sources gone"
        default: return String(kind)
        }
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(720, Math.max(400, (parent ? parent.width : 720) - 64))
    height: Math.min(640, Math.max(360, (parent ? parent.height : 640) - 64))
    x: Math.round(((parent ? parent.width : width) - width) / 2)
    y: Math.round(((parent ? parent.height : height) - height) / 2)
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: panel.message = ""

    background: Rectangle {
        color: panel.theme.controlSurfaceBg
        radius: 8
        border.width: 1
        border.color: panel.theme.panelBorder
    }

    header: Item {}
    footer: Item {}

    component PanelButton: Button {
        id: panelButton

        property bool strong: false

        implicitWidth: panelLabel.implicitWidth + 12
        implicitHeight: 22
        hoverEnabled: true
        padding: 0

        contentItem: Text {
            id: panelLabel

            text: panelButton.text
            color: !panelButton.enabled ? panel.theme.mutedText
                : panelButton.hovered || panelButton.strong ? panel.theme.appText : panel.theme.mutedText
            opacity: panelButton.enabled ? 1 : 0.5
            font.family: panel.theme.bodyFontFamily
            font.pixelSize: panel.theme.textMetadataSize
            font.weight: panel.theme.textWeightStrong
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 3
            color: panelButton.hovered && panelButton.enabled ? panel.theme.hoverBg : "transparent"
            border.width: panelButton.strong ? 1 : 0
            border.color: panel.theme.panelBorder
        }
    }

    component Caption: Text {
        Layout.fillWidth: true
        color: panel.theme.mutedText
        font.family: panel.theme.bodyFontFamily
        font.pixelSize: panel.theme.textMetadataSize
        wrapMode: Text.Wrap
    }

    component Heading: Text {
        Layout.fillWidth: true
        color: panel.theme.appText
        font.family: panel.theme.bodyFontFamily
        font.pixelSize: panel.theme.textCaptionSize
        font.weight: panel.theme.textWeightEmphasis
        wrapMode: Text.Wrap
    }

    component Card: Rectangle {
        Layout.fillWidth: true
        radius: 6
        color: panel.theme.surfaceBg
        border.width: 1
        border.color: panel.theme.quietBorder
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.leftMargin: 18
            Layout.rightMargin: 12
            spacing: 6

            Heading {
                text: "Librarians"
                font.pixelSize: panel.theme.textBodySize
            }

            Switch {
                objectName: "librarianEnabledSwitch"
                checked: panel.enabledHere
                text: checked ? "On" : "Off"
                font.pixelSize: panel.theme.textMetadataSize
                onToggled: panel.report(Wiki.setLibrarianEnabled(panel.libraryId, checked),
                    checked ? "Librarians are on for this Library." : "Librarians are off. Waiting work was cancelled.")
            }

            Item { Layout.fillWidth: true }

            PanelButton {
                text: panel.paused ? "Resume" : "Pause"
                visible: panel.enabledHere
                onClicked: panel.report(panel.paused ? Wiki.resumeLibrarians(panel.libraryId) : Wiki.pauseLibrarians(panel.libraryId),
                    panel.paused ? "Resumed." : "Paused. Running work was put back in the queue.")
            }

            PanelButton {
                text: "Close"
                onClicked: panel.close()
            }
        }

        Caption {
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            visible: panel.message.length > 0
            text: panel.message
            color: panel.theme.appText
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            clip: true

            ColumnLayout {
                width: panel.width - 40
                spacing: 8

                Caption {
                    visible: !panel.enabledHere
                    text: "Librarians keep this Library's Wiki up to date in the background: they re-check citations when files change, research parts the Wiki does not cover yet and look for contradictions. They only propose changes; you approve them. They never edit your files."
                }

                // Consent: required before any Library text goes to OpenAI.
                Card {
                    visible: panel.enabledHere
                    implicitHeight: consentRow.implicitHeight + 12

                    RowLayout {
                        id: consentRow

                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6

                        Caption {
                            text: panel.consented
                                ? "OpenAI research is allowed: librarians may send passages of this Library to OpenAI, within the budgets below."
                                : "Without your permission, librarians only do local checks. Allow OpenAI research to let them send passages of this Library to OpenAI to write and verify proposals."
                            color: panel.consented ? panel.theme.mutedText : panel.theme.warning
                        }

                        PanelButton {
                            objectName: "librarianConsentButton"
                            text: panel.consented ? "Revoke" : "Allow OpenAI research"
                            strong: !panel.consented
                            onClicked: panel.report(Wiki.setLibrarianConsent(panel.libraryId, !panel.consented),
                                panel.consented ? "Permission revoked. Nothing more will be sent." : "Allowed.")
                        }
                    }
                }

                // When they work.
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.enabledHere
                    spacing: 6

                    Caption {
                        Layout.fillWidth: false
                        text: "Works"
                    }

                    ComboBox {
                        id: modeBox

                        objectName: "librarianModeBox"
                        Layout.preferredWidth: 220
                        font.pixelSize: panel.theme.textMetadataSize
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { value: "manual", label: "Only when I start it" },
                            { value: "idle", label: "After changes and when idle" },
                            { value: "scheduled", label: "After changes and on a schedule" }
                        ]
                        currentIndex: Math.max(0, indexOfValue(panel.settings.mode || "manual"))
                        onActivated: panel.report(Wiki.updateLibrarianSettings(panel.libraryId, { mode: currentValue }))
                    }

                    Caption {
                        Layout.fillWidth: false
                        visible: panel.settings.mode === "scheduled"
                        text: "every"
                    }

                    SpinBox {
                        visible: panel.settings.mode === "scheduled"
                        from: 1
                        to: 168
                        value: Math.max(1, Math.round(Number(panel.settings.scheduleMinutes || 1440) / 60))
                        font.pixelSize: panel.theme.textMetadataSize
                        onValueModified: panel.report(Wiki.updateLibrarianSettings(panel.libraryId, { scheduleMinutes: value * 60 }))
                    }

                    Caption {
                        visible: panel.settings.mode === "scheduled"
                        text: "hours"
                    }
                }

                // Start a run now.
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.enabledHere
                    spacing: 4

                    PanelButton {
                        text: "Check sources now"
                        onClicked: panel.report(Wiki.startLibrarian(panel.libraryId, "maintenance"), "Maintenance started.")
                    }

                    PanelButton {
                        text: "Verify claims"
                        onClicked: panel.report(Wiki.startLibrarian(panel.libraryId, "verification"), "Verification started.")
                    }

                    TextField {
                        id: researchField

                        Layout.fillWidth: true
                        placeholderText: "Research a topic (optional)"
                        font.pixelSize: panel.theme.textMetadataSize
                        onAccepted: researchButton.clicked()
                    }

                    PanelButton {
                        id: researchButton

                        text: "Research"
                        strong: true
                        onClicked: {
                            panel.report(Wiki.startLibrarian(panel.libraryId, "research", researchField.text.trim()),
                                panel.consented ? "Research started. Proposals appear below for review."
                                                : "Surveying locally. Allow OpenAI research for proposals.")
                            researchField.text = ""
                        }
                    }
                }

                // Activity and spending.
                Heading {
                    visible: panel.enabledHere
                    Layout.topMargin: 4
                    text: "Activity"
                }

                Caption {
                    visible: panel.enabledHere
                    text: "Today: " + Number(panel.today.calls || 0) + " of " + Number(panel.settings.dailyCallBudget || 0) + " calls, "
                        + Number(panel.today.tokens || 0).toLocaleString(Qt.locale(), "f", 0) + " of "
                        + Number(panel.settings.dailyTokenBudget || 0).toLocaleString(Qt.locale(), "f", 0) + " tokens"
                        + " · last 30 days about $" + Number(panel.month.costUsd || 0).toFixed(2) + " (estimate)"
                        + (panel.settings.lastError ? " · last error: " + panel.settings.lastError : "")
                        + (panel.status.interactiveBusy ? " · waiting for the chat to finish" : "")
                        + (panel.paused ? " · paused" : "")
                }

                Repeater {
                    model: panel.enabledHere ? (panel.status.runs || []) : []

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 4

                        Caption {
                            text: panel.runLabel(modelData) + (modelData.errorMessage ? " — " + modelData.errorMessage : "")
                            color: modelData.status === "running" ? panel.theme.accent
                                : modelData.status === "failed" ? panel.theme.danger : panel.theme.mutedText
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        PanelButton {
                            text: "Cancel"
                            visible: modelData.status === "running"
                            onClicked: panel.report(Wiki.cancelLibrarianRun(modelData.id), "Cancelled.")
                        }
                    }
                }

                // Budgets.
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.enabledHere
                    spacing: 4

                    Caption {
                        Layout.fillWidth: false
                        text: "Per run, calls"
                    }

                    SpinBox {
                        from: 0
                        to: 50
                        value: Number(panel.settings.maxCallsPerRun || 0)
                        font.pixelSize: panel.theme.textMetadataSize
                        onValueModified: panel.report(Wiki.updateLibrarianSettings(panel.libraryId, { maxCallsPerRun: value }))
                    }

                    Caption {
                        Layout.fillWidth: false
                        text: "Per day, calls"
                    }

                    SpinBox {
                        from: 0
                        to: 1000
                        value: Number(panel.settings.dailyCallBudget || 0)
                        font.pixelSize: panel.theme.textMetadataSize
                        onValueModified: panel.report(Wiki.updateLibrarianSettings(panel.libraryId, { dailyCallBudget: value }))
                    }

                    Caption {
                        Layout.fillWidth: false
                        text: "Depth"
                    }

                    SpinBox {
                        from: 0
                        to: 5
                        value: Number(panel.settings.maxDepth || 0)
                        font.pixelSize: panel.theme.textMetadataSize
                        onValueModified: panel.report(Wiki.updateLibrarianSettings(panel.libraryId, { maxDepth: value }))
                    }

                    Item { Layout.fillWidth: true }
                }

                // What they propose.
                Heading {
                    Layout.topMargin: 4
                    visible: panel.proposalList.length > 0
                    text: "Proposed changes (" + panel.proposalList.length + ")"
                }

                Repeater {
                    model: panel.proposalList

                    delegate: Card {
                        required property var modelData

                        implicitHeight: proposalColumn.implicitHeight + 12

                        ColumnLayout {
                            id: proposalColumn

                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 4

                            Heading { text: modelData.title }
                            Caption {
                                text: modelData.explanation
                                maximumLineCount: 4
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: String(modelData.diff || "").length > 0
                                text: String(modelData.diff || "")
                                color: panel.theme.appText
                                font.family: "monospace"
                                font.pixelSize: panel.theme.textMetadataSize
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 12
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                spacing: 4
                                PanelButton {
                                    text: "Approve"
                                    strong: true
                                    onClicked: {
                                        const result = Wiki.approve(modelData.id)
                                        panel.report(result, result.status === "conflicted"
                                            ? "The page changed since this was proposed, so nothing was applied."
                                            : "Applied.")
                                    }
                                }
                                PanelButton {
                                    text: "Reject"
                                    onClicked: panel.report(Wiki.reject(modelData.id), "Rejected. The librarians will not propose it again unless the sources change.")
                                }
                            }
                        }
                    }
                }

                // What they found.
                Heading {
                    Layout.topMargin: 4
                    text: "Findings (" + panel.findingList.length + ")"
                }

                Caption {
                    visible: panel.findingList.length === 0
                    text: "Nothing to look at. Findings are things the librarians noticed: stale sources, unsupported or contradictory claims, and parts of the Library the Wiki does not cover."
                }

                Repeater {
                    model: panel.findingList

                    delegate: Card {
                        required property var modelData

                        implicitHeight: findingColumn.implicitHeight + 10

                        ColumnLayout {
                            id: findingColumn

                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: panel.theme.appText
                                font.family: panel.theme.bodyFontFamily
                                font.pixelSize: panel.theme.textCaptionSize
                                wrapMode: Text.Wrap
                            }
                            Caption {
                                text: panel.kindLabel(modelData.kind) + " · " + modelData.librarian
                                    + (modelData.status === "proposed" ? " · proposal waiting" : "")
                                    + (modelData.detail ? " — " + modelData.detail : "")
                                color: modelData.kind === "contradiction" ? panel.theme.warning : panel.theme.mutedText
                                maximumLineCount: 4
                                elide: Text.ElideRight
                            }
                            Repeater {
                                model: (modelData.evidence || []).slice(0, 3)

                                delegate: Caption {
                                    required property var modelData

                                    text: "↳ " + modelData.relativePath
                                        + (modelData.startLine ? ":" + modelData.startLine : "")
                                        + (modelData.quote ? " — “" + modelData.quote + "”" : "")
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                            PanelButton {
                                text: "Dismiss"
                                onClicked: panel.report(Wiki.dismissFinding(modelData.id), "Dismissed. It will not be raised again for the same sources.")
                            }
                        }
                    }
                }
            }
        }
    }
}
