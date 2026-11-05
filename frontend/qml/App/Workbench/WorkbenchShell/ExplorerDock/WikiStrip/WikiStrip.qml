import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

// The Living Wiki of the selected Library: how many pages and open
// proposals it has, and a review window for writing pages, approving or
// rejecting proposals, checking claims and following jobs.
//
// Nothing here writes to the Library folder. Pages live in Archivist's
// database and show under a virtual "Wiki/" path; every AI change waits for
// the person's approval.
Rectangle {
    id: root

    required property var theme
    required property string libraryId

    readonly property var overview: {
        Wiki.revision
        return root.libraryId.length > 0 ? Wiki.overview(root.libraryId) : ({})
    }
    readonly property int pageCount: Number(root.overview.pages || 0)
    readonly property int pendingCount: Number(root.overview.pendingProposals || 0)
    readonly property int activeJobs: Number(root.overview.activeJobs || 0)
    readonly property int staleCount: Number(root.overview.stale || 0)

    function label() {
        if (root.activeJobs > 0)
            return "Wiki: working on " + root.activeJobs + (root.activeJobs === 1 ? " job" : " jobs")
        if (root.pageCount === 0)
            return "Wiki: no pages yet"
        let text = "Wiki: " + root.pageCount + (root.pageCount === 1 ? " page" : " pages")
        if (root.pendingCount > 0)
            text += " · " + root.pendingCount + " to review"
        if (root.staleCount > 0)
            text += " · " + root.staleCount + " stale"
        return text
    }

    implicitHeight: row.implicitHeight + 8
    color: root.theme.surfaceBg
    visible: Wiki.ready && root.libraryId.length > 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.quietBorder
        opacity: 0.7
    }

    RowLayout {
        id: row

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 7
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.activeJobs > 0 ? root.theme.accent
                : root.pendingCount > 0 || root.staleCount > 0 ? root.theme.warning
                : root.pageCount > 0 ? root.theme.success
                : root.theme.mutedText
        }

        Text {
            Layout.fillWidth: true
            text: root.label()
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightStrong
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        WikiButton {
            objectName: "wikiLibrariansButton"
            text: "Librarians…"
            onClicked: librarians.open()
        }

        WikiButton {
            objectName: "wikiOpenButton"
            text: "Open…"
            onClicked: review.open()
        }
    }

    LibrarianPanel {
        id: librarians

        theme: root.theme
        libraryId: root.libraryId
    }

    component WikiButton: Button {
        id: wikiButton

        property bool strong: false

        implicitWidth: wikiLabel.implicitWidth + 12
        implicitHeight: 22
        hoverEnabled: true
        padding: 0

        contentItem: Text {
            id: wikiLabel

            text: wikiButton.text
            color: !wikiButton.enabled ? root.theme.mutedText
                : wikiButton.hovered || wikiButton.strong ? root.theme.appText : root.theme.mutedText
            opacity: wikiButton.enabled ? 1 : 0.5
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightStrong
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 3
            color: wikiButton.hovered && wikiButton.enabled ? root.theme.hoverBg : "transparent"
            border.width: wikiButton.strong ? 1 : 0
            border.color: root.theme.panelBorder
        }
    }

    component Caption: Text {
        Layout.fillWidth: true
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textMetadataSize
        wrapMode: Text.Wrap
    }

    component Heading: Text {
        Layout.fillWidth: true
        color: root.theme.appText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textCaptionSize
        font.weight: root.theme.textWeightEmphasis
        wrapMode: Text.Wrap
    }

    function statusColor(status) {
        switch (String(status)) {
        case "verified":
        case "supported":
        case "applied":
        case "completed":
            return root.theme.success
        case "stale":
        case "contested":
        case "conflicted":
        case "needs-review":
            return root.theme.warning
        case "failed":
        case "retracted":
            return root.theme.danger
        default:
            return root.theme.mutedText
        }
    }

    function citationLabel(citation) {
        const lines = citation.startLine ? ":" + citation.startLine + (citation.endLine && citation.endLine !== citation.startLine ? "-" + citation.endLine : "") : ""
        return citation.relativePath + lines + (citation.status !== "valid" ? " (" + citation.status + ")" : "")
    }

    Dialog {
        id: review

        objectName: "wikiReviewDialog"
        property string selectedPageId: ""
        property string message: ""
        readonly property var detail: {
            Wiki.revision
            return review.selectedPageId.length > 0 ? Wiki.page(review.selectedPageId) : ({})
        }
        readonly property var pageList: {
            Wiki.revision
            return review.opened ? Wiki.pages(root.libraryId) : []
        }
        readonly property var pending: {
            Wiki.revision
            return review.opened ? Wiki.proposals(root.libraryId, "pending") : []
        }
        readonly property var jobList: {
            Wiki.revision
            return review.opened ? Wiki.jobs(root.libraryId, 12) : []
        }

        function report(result, success) {
            review.message = result && result.ok === false ? String(result.message || "That did not work.") : (success || "")
        }

        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(880, Math.max(420, (parent ? parent.width : 880) - 64))
        height: Math.min(680, Math.max(360, (parent ? parent.height : 680) - 64))
        x: Math.round(((parent ? parent.width : width) - width) / 2)
        y: Math.round(((parent ? parent.height : height) - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape
        onOpened: review.message = ""

        background: Rectangle {
            color: root.theme.controlSurfaceBg
            radius: 8
            border.width: 1
            border.color: root.theme.panelBorder
        }

        header: Item {}
        footer: Item {}

        contentItem: ColumnLayout {
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Layout.leftMargin: 18
                Layout.rightMargin: 12
                spacing: 6

                Heading {
                    text: "Wiki"
                    font.pixelSize: root.theme.textBodySize
                }

                TextField {
                    id: topicField

                    objectName: "wikiTopicField"
                    Layout.preferredWidth: 260
                    placeholderText: "Topic for a new page"
                    font.pixelSize: root.theme.textCaptionSize
                    onAccepted: generateButton.clicked()
                }

                WikiButton {
                    id: generateButton

                    text: "Write page"
                    strong: true
                    enabled: topicField.text.trim().length > 0
                    onClicked: {
                        const result = Wiki.generate(root.libraryId, { topic: topicField.text.trim() })
                        review.report(result, "Writing a proposal from the Library's sources. It appears below for review.")
                        if (result.ok !== false)
                            topicField.text = ""
                    }
                }

                WikiButton {
                    text: "Check sources"
                    onClicked: review.report(Wiki.reconcile(root.libraryId), "Checking every citation against the current files.")
                }

                WikiButton {
                    text: "Close"
                    onClicked: review.close()
                }
            }

            Caption {
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                visible: review.message.length > 0
                text: review.message
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12

                // Left: proposals to review, pages, recent jobs.
                ScrollView {
                    SplitView.preferredWidth: 320
                    clip: true

                    ColumnLayout {
                        width: 300
                        spacing: 6

                        Heading { text: "To review (" + review.pending.length + ")" }

                        Repeater {
                            model: review.pending

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                implicitHeight: proposalColumn.implicitHeight + 12
                                radius: 6
                                color: root.theme.surfaceBg
                                border.width: 1
                                border.color: root.theme.quietBorder

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
                                        color: root.theme.appText
                                        font.family: "monospace"
                                        font.pixelSize: root.theme.textMetadataSize
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 18
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        spacing: 4
                                        WikiButton {
                                            text: "Approve"
                                            strong: true
                                            onClicked: {
                                                const result = Wiki.approve(modelData.id)
                                                review.report(result, result.status === "conflicted"
                                                    ? "The page changed since this was proposed, so nothing was applied. Refresh the page to get a new proposal."
                                                    : "Applied.")
                                                if (result.pageId)
                                                    review.selectedPageId = result.pageId
                                            }
                                        }
                                        WikiButton {
                                            text: "Reject"
                                            onClicked: review.report(Wiki.reject(modelData.id), "Rejected. Nothing changed.")
                                        }
                                    }
                                }
                            }
                        }

                        Heading {
                            Layout.topMargin: 8
                            text: "Pages (" + review.pageList.length + ")"
                        }

                        Repeater {
                            model: review.pageList

                            delegate: ItemDelegate {
                                required property var modelData

                                Layout.fillWidth: true
                                highlighted: review.selectedPageId === modelData.id
                                onClicked: review.selectedPageId = modelData.id

                                contentItem: ColumnLayout {
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        color: root.theme.appText
                                        font.family: root.theme.bodyFontFamily
                                        font.pixelSize: root.theme.textCaptionSize
                                        elide: Text.ElideRight
                                    }
                                    Caption {
                                        text: modelData.virtualPath + " · " + modelData.claimCount + " claims"
                                            + (modelData.staleClaimCount > 0 ? " · " + modelData.staleClaimCount + " stale" : "")
                                            + (modelData.reviewStatus === "needs-review" ? " · needs review" : "")
                                        color: modelData.reviewStatus === "needs-review" ? root.theme.warning : root.theme.mutedText
                                    }
                                }
                            }
                        }

                        Heading {
                            Layout.topMargin: 8
                            text: "Recent jobs"
                        }

                        Repeater {
                            model: review.jobList

                            delegate: RowLayout {
                                required property var modelData

                                Layout.fillWidth: true
                                spacing: 4

                                Caption {
                                    text: modelData.jobType + (modelData.request && modelData.request.topic ? " “" + modelData.request.topic + "”" : "")
                                        + " · " + modelData.status
                                        + (modelData.errorMessage ? " — " + modelData.errorMessage : "")
                                    color: root.statusColor(modelData.status)
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                WikiButton {
                                    text: "Cancel"
                                    visible: modelData.status === "queued" || modelData.status === "running"
                                    onClicked: review.report(Wiki.cancelJob(modelData.id))
                                }
                                WikiButton {
                                    text: "Retry"
                                    visible: modelData.status === "failed" || modelData.status === "cancelled"
                                    onClicked: review.report(Wiki.retryJob(modelData.id))
                                }
                            }
                        }
                    }
                }

                // Right: the selected page, its claims and their sources.
                ScrollView {
                    SplitView.fillWidth: true
                    clip: true

                    ColumnLayout {
                        width: Math.max(300, review.width - 380)
                        spacing: 6

                        Caption {
                            visible: review.selectedPageId.length === 0
                            text: "Pick a page to see its claims and sources. AI-written text is an interpretation; the cited Library files are the originals."
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: review.selectedPageId.length > 0
                            spacing: 4

                            Heading {
                                text: (review.detail.title || "") + "  ·  revision " + (review.detail.revision || 0)
                                font.pixelSize: root.theme.textBodySize
                            }
                            WikiButton {
                                text: "Refresh from sources"
                                onClicked: review.report(Wiki.expand(root.libraryId, review.selectedPageId), "Writing a revision proposal from the current sources.")
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: review.selectedPageId.length > 0
                            text: String(review.detail.content || "")
                            textFormat: Text.MarkdownText
                            color: root.theme.appText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textCaptionSize
                            wrapMode: Text.Wrap
                        }

                        Heading {
                            Layout.topMargin: 6
                            visible: review.selectedPageId.length > 0
                            text: "Claims"
                        }

                        Repeater {
                            model: review.detail.claims || []

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                implicitHeight: claimColumn.implicitHeight + 10
                                radius: 5
                                color: "transparent"
                                border.width: 1
                                border.color: root.theme.quietBorder

                                ColumnLayout {
                                    id: claimColumn

                                    anchors.fill: parent
                                    anchors.margins: 5
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.statement
                                        color: modelData.verificationStatus === "retracted" ? root.theme.mutedText : root.theme.appText
                                        font.strikeout: modelData.verificationStatus === "retracted"
                                        font.family: root.theme.bodyFontFamily
                                        font.pixelSize: root.theme.textCaptionSize
                                        wrapMode: Text.Wrap
                                    }
                                    Caption {
                                        text: modelData.claimType + " · " + modelData.verificationStatus
                                            + " · confidence " + Number(modelData.confidence || 0).toFixed(2)
                                            + (modelData.statusReason ? " · " + modelData.statusReason : "")
                                        color: root.statusColor(modelData.verificationStatus)
                                    }
                                    Repeater {
                                        model: modelData.citations || []

                                        delegate: Caption {
                                            required property var modelData

                                            text: "↳ " + root.citationLabel(modelData) + (modelData.quote ? " — “" + modelData.quote + "”" : "")
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                    }
                                    RowLayout {
                                        spacing: 4
                                        WikiButton {
                                            text: "Verify"
                                            visible: modelData.verificationStatus !== "verified" && modelData.verificationStatus !== "retracted"
                                            onClicked: review.report(Wiki.reviewClaim(modelData.id, "verify"))
                                        }
                                        WikiButton {
                                            text: "Retract"
                                            visible: modelData.verificationStatus !== "retracted"
                                            onClicked: review.report(Wiki.reviewClaim(modelData.id, "retract"))
                                        }
                                        WikiButton {
                                            text: "Reopen"
                                            visible: modelData.verificationStatus === "retracted" || modelData.verificationStatus === "verified"
                                            onClicked: review.report(Wiki.reviewClaim(modelData.id, "reopen"))
                                        }
                                    }
                                }
                            }
                        }

                        Heading {
                            Layout.topMargin: 6
                            visible: (review.detail.revisions || []).length > 1
                            text: "History"
                        }

                        Repeater {
                            model: (review.detail.revisions || []).length > 1 ? review.detail.revisions : []

                            delegate: RowLayout {
                                required property var modelData
                                required property int index

                                Layout.fillWidth: true
                                spacing: 4

                                Caption {
                                    text: "Revision " + modelData.revision + " · " + modelData.changeKind + " by " + modelData.actorType
                                        + " · " + modelData.createdAt
                                }
                                WikiButton {
                                    text: "Restore"
                                    visible: index > 0
                                    onClicked: review.report(Wiki.restoreRevision(review.selectedPageId, modelData.id), "Restored as a new revision.")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
