import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var theme
    required property var proposal
    required property bool busy
    required property bool reviewReady

    signal approveAllRequested(string proposalId)
    signal approveSelectedRequested(string proposalId, var operationIds)
    signal rejectRequested(string proposalId)
    signal undoRequested(string proposalId)

    property var selectedOperationIds: []

    readonly property string proposalId: String(proposal.id || "")
    readonly property string proposalStatus: String(proposal.status || "proposed")
    readonly property var operationValues: proposal.operations && proposal.operations.length !== undefined
        ? proposal.operations
        : []
    readonly property bool canReview: proposalStatus === "proposed"
        && reviewReady
        && !busy
    readonly property bool canUndo: proposalStatus === "completed" && !busy
    readonly property string stateLabel: {
        if (busy) {
            return proposalStatus === "completed" ? "UNDOING" : "APPLYING"
        }

        if (proposalStatus === "proposed") {
            return reviewReady ? "REVIEW" : "COLLECTING"
        }

        if (proposalStatus === "completed") {
            return "APPLIED"
        }

        if (proposalStatus === "undone") {
            return "UNDONE"
        }

        if (proposalStatus === "rejected") {
            return "REJECTED"
        }

        if (proposalStatus === "stale") {
            return "STALE"
        }

        if (proposalStatus === "failed") {
            return "FAILED"
        }

        return proposalStatus.toUpperCase()
    }
    readonly property color stateColor: {
        if (proposalStatus === "completed") {
            return theme.success
        }

        if (proposalStatus === "failed" || proposalStatus === "stale") {
            return theme.danger
        }

        if (proposalStatus === "rejected" || proposalStatus === "undone") {
            return theme.mutedText
        }

        return theme.accentBright
    }

    function operationId(operation) {
        return String(operation && operation.id ? operation.id : "")
    }

    function isSelected(operationId) {
        return selectedOperationIds.indexOf(String(operationId || "")) >= 0
    }

    function toggleSelection(operationId) {
        var id = String(operationId || "")

        if (!canReview || id.length === 0) {
            return
        }

        var next = selectedOperationIds.slice()
        var index = next.indexOf(id)

        if (index >= 0) {
            next.splice(index, 1)
        } else {
            next.push(id)
        }

        selectedOperationIds = next
    }

    function resetSelection() {
        if (proposalStatus !== "proposed") {
            selectedOperationIds = []
            return
        }

        var next = []

        for (var index = 0; index < operationValues.length; index += 1) {
            var id = operationId(operationValues[index])

            if (id.length > 0) {
                next.push(id)
            }
        }

        selectedOperationIds = next
    }

    function operationTitle(operation) {
        var type = String(operation && operation.type ? operation.type : "edit")

        if (type === "create_file") {
            return "Create file"
        }

        if (type === "patch_file") {
            return "Patch file"
        }

        if (type === "rename_file") {
            return "Rename file"
        }

        if (type === "move_file") {
            return "Move file"
        }

        if (type === "create_directory") {
            return "Create directory"
        }

        return type.replace(/_/g, " ")
    }

    function operationPath(operation) {
        var source = String(operation && operation.sourcePath ? operation.sourcePath : "")
        var destination = String(operation && operation.destinationPath ? operation.destinationPath : "")

        if (source.length > 0 && destination.length > 0 && source !== destination) {
            return source + "  →  " + destination
        }

        return destination.length > 0 ? destination : source
    }

    function operationStatus(operation) {
        return String(operation && operation.status ? operation.status : proposalStatus)
            .replace(/_/g, " ")
            .toUpperCase()
    }

    function previewValue(value) {
        if (value === null || value === undefined) {
            return ""
        }

        var text = String(value)
        return text.length > 0 ? text : "(empty file)"
    }

    function hasPreview(operation) {
        return proposalStatus === "proposed"
            && operation
            && (
                operation.beforeContent !== null
                && operation.beforeContent !== undefined
                || operation.afterContent !== null
                && operation.afterContent !== undefined
            )
    }

    onProposalChanged: resetSelection()
    Component.onCompleted: resetSelection()

    implicitHeight: contentColumn.implicitHeight + 24
    radius: theme.radiusMedium
    color: theme.controlSurfaceBg
    border.width: 1
    border.color: proposalStatus === "stale" || proposalStatus === "failed"
        ? theme.danger
        : proposalStatus === "completed"
            ? theme.success
            : theme.quietBorder
    clip: true

    Column {
        id: contentColumn

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 10

        Item {
            width: parent.width
            height: 24

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: root.stateColor
                    opacity: root.proposalStatus === "proposed" ? 1.0 : 0.78
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "AI EDIT PROPOSAL"
                    color: root.theme.appText
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.DemiBold
                    font.letterSpacing: root.theme.textTrackingCaps
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: stateText.implicitWidth + 14
                height: 20
                radius: 5
                color: "transparent"
                border.width: 1
                border.color: root.stateColor
                opacity: 0.86

                Text {
                    id: stateText

                    anchors.centerIn: parent
                    text: root.stateLabel
                    color: root.stateColor
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.typeSize(8)
                    font.weight: Font.DemiBold
                    font.letterSpacing: root.theme.textTrackingLabel
                }
            }
        }

        Text {
            width: parent.width
            text: String(root.proposal.summary || "Review proposed Library changes")
            color: root.theme.appText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(11)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: root.proposalStatus === "proposed" && !root.reviewReady
            text: "Archivist is still collecting changes for this Run. Review unlocks when the response finishes."
            color: root.theme.mutedText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        Text {
            width: parent.width
            visible: String(root.proposal.errorMessage || "").length > 0
            text: String(root.proposal.errorMessage || "")
            color: root.theme.danger
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            wrapMode: Text.Wrap
        }

        Column {
            id: operationsColumn

            width: parent.width
            spacing: 8

            Repeater {
                model: root.operationValues

                delegate: Rectangle {
                    id: operationCard

                    readonly property var operation: modelData || ({})
                    readonly property string operationId: root.operationId(operation)
                    readonly property bool selected: root.isSelected(operationId)

                    width: operationsColumn.width
                    implicitHeight: operationContent.implicitHeight + 18
                    radius: root.theme.radiusSmall
                    color: root.canReview && selected
                        ? root.theme.activeBg
                        : root.theme.codeBlockBg
                    border.width: 1
                    border.color: root.canReview && selected
                        ? root.theme.accent
                        : root.theme.quietBorder

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.canReview
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.toggleSelection(operationCard.operationId)
                    }

                    Column {
                        id: operationContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 9
                        spacing: 6

                        Item {
                            width: parent.width
                            height: 20

                            Row {
                                anchors.left: parent.left
                                anchors.right: operationState.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                Rectangle {
                                    visible: root.proposalStatus === "proposed"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 15
                                    height: 15
                                    radius: 3
                                    color: operationCard.selected
                                        ? root.theme.accent
                                        : "transparent"
                                    border.width: 1
                                    border.color: operationCard.selected
                                        ? root.theme.accentBright
                                        : root.theme.mutedText
                                    opacity: root.reviewReady ? 1.0 : 0.5

                                    Text {
                                        anchors.centerIn: parent
                                        visible: operationCard.selected
                                        text: "✓"
                                        color: root.theme.appBgDeep
                                        font.family: root.theme.chatFontFamily
                                        font.pixelSize: root.theme.typeSize(8)
                                        font.weight: Font.Bold
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.operationTitle(operationCard.operation)
                                    color: root.theme.appText
                                    font.family: root.theme.chatFontFamily
                                    font.pixelSize: root.theme.typeSize(9)
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                id: operationState

                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.operationStatus(operationCard.operation)
                                color: String(operationCard.operation.status || "") === "failed"
                                    ? root.theme.danger
                                    : String(operationCard.operation.status || "") === "completed"
                                        ? root.theme.success
                                        : root.theme.mutedText
                                font.family: root.theme.chatFontFamily
                                font.pixelSize: root.theme.typeSize(8)
                                font.weight: Font.DemiBold
                                opacity: 0.75
                            }
                        }

                        Text {
                            width: parent.width
                            text: root.operationPath(operationCard.operation)
                            color: root.theme.codeBlockMutedText
                            font.family: root.theme.monospaceFontFamily
                            font.pixelSize: root.theme.typeSize(9)
                            elide: Text.ElideMiddle
                        }

                        Column {
                            width: parent.width
                            visible: root.hasPreview(operationCard.operation)
                            spacing: 6

                            Rectangle {
                                width: parent.width
                                implicitHeight: beforeText.implicitHeight + 16
                                visible: operationCard.operation.beforeContent !== null
                                    && operationCard.operation.beforeContent !== undefined
                                radius: 4
                                color: root.theme.workspaceBgDeep
                                border.width: 1
                                border.color: root.theme.quietBorder

                                Text {
                                    id: beforeText

                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    text: "BEFORE\n" + root.previewValue(operationCard.operation.beforeContent)
                                    color: root.theme.codeBlockMutedText
                                    font.family: root.theme.monospaceFontFamily
                                    font.pixelSize: root.theme.typeSize(8)
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 6
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                width: parent.width
                                implicitHeight: afterText.implicitHeight + 16
                                visible: operationCard.operation.afterContent !== null
                                    && operationCard.operation.afterContent !== undefined
                                radius: 4
                                color: root.theme.workspaceBgDeep
                                border.width: 1
                                border.color: root.theme.quietBorder

                                Text {
                                    id: afterText

                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    text: "AFTER\n" + root.previewValue(operationCard.operation.afterContent)
                                    color: root.theme.codeBlockText
                                    font.family: root.theme.monospaceFontFamily
                                    font.pixelSize: root.theme.typeSize(8)
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 6
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            visible: String(operationCard.operation.errorMessage || "").length > 0
                            text: String(operationCard.operation.errorMessage || "")
                            color: root.theme.danger
                            font.family: root.theme.chatFontFamily
                            font.pixelSize: root.theme.typeSize(8)
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: actionRow.implicitHeight
            visible: root.proposalStatus === "proposed" || root.proposalStatus === "completed"

            Row {
                id: actionRow

                anchors.right: parent.right
                spacing: 8

                Button {
                    visible: root.proposalStatus === "proposed"
                    width: 72
                    height: 28
                    text: "Reject"
                    enabled: root.canReview
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.rejectRequested(root.proposalId)

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? parent.hovered
                                ? root.theme.danger
                                : root.theme.mutedText
                            : root.theme.mutedText
                        opacity: parent.enabled ? 1.0 : 0.45
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: parent.hovered && parent.enabled
                            ? root.theme.hoverBg
                            : "transparent"
                        border.width: 1
                        border.color: parent.hovered && parent.enabled
                            ? root.theme.danger
                            : root.theme.quietBorder
                    }
                }

                Button {
                    visible: root.proposalStatus === "proposed"
                    width: 126
                    height: 28
                    text: "Approve Selected"
                    enabled: root.canReview && root.selectedOperationIds.length > 0
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.approveSelectedRequested(
                        root.proposalId,
                        root.selectedOperationIds.slice()
                    )

                    contentItem: Text {
                        text: parent.text
                        color: root.theme.appText
                        opacity: parent.enabled ? 1.0 : 0.42
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: parent.hovered && parent.enabled
                            ? root.theme.hoverBg
                            : root.theme.surfaceBg
                        border.width: 1
                        border.color: parent.hovered && parent.enabled
                            ? root.theme.accentBright
                            : root.theme.panelBorder
                    }
                }

                Button {
                    visible: root.proposalStatus === "proposed"
                    width: 96
                    height: 28
                    text: root.busy ? "Applying…" : "Approve All"
                    enabled: root.canReview
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.approveAllRequested(root.proposalId)

                    contentItem: Text {
                        text: parent.text
                        color: root.theme.appBgDeep
                        opacity: parent.enabled ? 1.0 : 0.48
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: root.theme.accentBright
                        opacity: parent.enabled
                            ? parent.hovered ? 1.0 : 0.9
                            : 0.36
                    }
                }

                Button {
                    visible: root.proposalStatus === "completed"
                    width: 72
                    height: 28
                    text: root.busy ? "Undoing…" : "Undo"
                    enabled: root.canUndo
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.undoRequested(root.proposalId)

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? root.theme.appText
                            : root.theme.mutedText
                        opacity: parent.enabled ? 1.0 : 0.45
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.typeSize(9)
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: parent.hovered && parent.enabled
                            ? root.theme.hoverBg
                            : "transparent"
                        border.width: 1
                        border.color: parent.hovered && parent.enabled
                            ? root.theme.accentBright
                            : root.theme.quietBorder
                    }
                }
            }
        }
    }
}
