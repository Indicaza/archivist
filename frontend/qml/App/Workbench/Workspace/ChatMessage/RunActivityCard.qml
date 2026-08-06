import QtQuick

Rectangle {
    id: root

    required property var theme
    required property var activity
    required property var attachedFiles
    required property string progressLabel

    function listValue(value) {
        return value && value.length !== undefined ? value : []
    }

    function sourcePath(source) {
        if (!source) {
            return ""
        }

        var metadata = source.metadata || ({})
        var value = String(
            source.relativePath
            || metadata.relativePath
            || source.fileName
            || metadata.fileName
            || source.label
            || ""
        )

        return value
            .replace(/\\/g, "/")
            .replace(/:\d+(?:-\d+)?$/, "")
    }

    function compactSourcePath(source) {
        var parts = root.sourcePath(source).split("/").filter(function(part) {
            return part.length > 0
        })

        if (parts.length <= 2) {
            return parts.join("/")
        }

        return parts.slice(parts.length - 2).join("/")
    }

    function aggregateRetrievedFiles(sources) {
        var values = root.listValue(sources)
        var files = []
        var indexes = ({})

        for (var index = 0; index < values.length; index += 1) {
            var source = values[index] || ({})
            var metadata = source.metadata || ({})
            var path = root.sourcePath(source)
            var key = String(metadata.fileId || source.fileId || path)

            if (key.length === 0) {
                continue
            }

            if (indexes[key] === undefined) {
                indexes[key] = files.length
                files.push({
                    key: key,
                    label: root.compactSourcePath(source),
                    passageCount: 1
                })
                continue
            }

            files[indexes[key]].passageCount += 1
        }

        return files
    }

    function formattedTokenCount(value) {
        var count = Math.max(0, Number(value || 0))

        if (count >= 1000) {
            return (count / 1000).toFixed(1)
                .replace(/\.0$/, "")
                + "k"
        }

        return String(Math.round(count))
    }

    function humanizeToolId(value) {
        return String(value || "Library tool")
            .replace(/_/g, " ")
            .replace(/\b\w/g, function(character) {
                return character.toUpperCase()
            })
    }

    function toolLabel(execution) {
        return String(
            execution.toolName
            || root.humanizeToolId(execution.toolId)
        )
    }

    function toolRowState(execution) {
        var status = String(execution.status || "requested")

        if (status === "requested" || status === "running") {
            return "active"
        }

        if (status === "failed" || status === "cancelled") {
            return "warning"
        }

        return "complete"
    }

    function toolDetail(execution) {
        var status = String(execution.status || "requested")
        var output = execution.output || ({})
        var toolId = String(execution.toolId || "")

        if (status === "requested") {
            return "Queued"
        }

        if (status === "running") {
            return "Reading Library"
        }

        if (status === "failed" || status === "cancelled") {
            return String(execution.errorCode || status)
                .replace(/_/g, " ")
        }

        if (toolId === "search_library") {
            return Number(output.matchCount || 0) + " passages · "
                + Number(output.fileCount || 0) + " files"
        }

        if (toolId === "search_filenames") {
            return Number(output.matchCount || 0) + " files"
        }

        if (toolId === "list_directory") {
            return Number(output.entryCount || 0) + " entries"
        }

        if (toolId === "read_file_range") {
            return String(output.relativePath || "File range")
                + " · " + Number(output.startLine || 0)
                + "-" + Number(output.endLine || 0)
        }

        if (toolId === "read_file") {
            return String(output.relativePath || "File read")
        }

        return "Complete"
    }

    function hasActiveTool(executions) {
        var values = root.listValue(executions)

        for (var index = 0; index < values.length; index += 1) {
            var status = String(values[index].status || "")

            if (status === "requested" || status === "running") {
                return true
            }
        }

        return false
    }

    readonly property var runActivity: activity || ({})
    readonly property var activityAttachedFiles: {
        var snapshot = root.listValue(root.runActivity.attachedFiles)

        return snapshot.length > 0
            ? snapshot
            : root.listValue(root.attachedFiles)
    }
    readonly property var retrievedFiles: root.aggregateRetrievedFiles(
        root.runActivity.retrievedSources
    )
    readonly property int visibleAttachmentCount: Math.min(
        3,
        activityAttachedFiles.length
    )
    readonly property int retrievedFileBudget: Math.max(
        2,
        5 - visibleAttachmentCount
    )
    readonly property int visibleRetrievedCount: Math.min(
        retrievedFileBudget,
        retrievedFiles.length
    )
    readonly property int retrievedSourceCount: Number(
        runActivity.retrievedSourceCount || 0
    )
    readonly property int retrievedFileCount: Number(
        runActivity.retrievedFileCount || 0
    )
    readonly property int warningCount: Number(
        runActivity.warningCount || 0
    )
    readonly property var toolExecutions: root.listValue(
        runActivity.toolExecutions
    )
    readonly property int visibleToolCount: Math.min(
        4,
        toolExecutions.length
    )
    readonly property var visibleToolExecutions: toolExecutions.slice(
        Math.max(0, toolExecutions.length - visibleToolCount)
    )
    readonly property bool activeToolPresent: root.hasActiveTool(
        toolExecutions
    )
    readonly property string modelLabel: {
        var provider = String(root.runActivity.provider || "")
        var model = String(root.runActivity.model || "")

        if (provider.length > 0 && model.length > 0) {
            return provider + " · " + model
        }

        return model.length > 0 ? model : provider
    }
    readonly property string phaseLabel: progressLabel.trim().length > 0
        ? progressLabel
        : "Preparing response…"

    implicitHeight: layout.implicitHeight + 24
    radius: theme.radiusSmall
    color: theme.controlSurfaceBg
    border.width: 1
    border.color: theme.quietBorder
    clip: true

    Column {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        Item {
            width: parent.width
            height: 22

            Text {
                id: modelText

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(parent.width * 0.44, implicitWidth)
                text: root.modelLabel
                color: root.theme.mutedText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.typeSize(8)
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideMiddle
                opacity: 0.68
            }

            Row {
                anchors.left: parent.left
                anchors.right: modelText.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: root.theme.accentBright

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: root.visible

                        NumberAnimation {
                            from: 0.38
                            to: 1
                            duration: 620
                            easing.type: Easing.InOutSine
                        }

                        NumberAnimation {
                            from: 1
                            to: 0.38
                            duration: 620
                            easing.type: Easing.InOutSine
                        }
                    }
                }

                Text {
                    width: Math.max(0, parent.width - 15)
                    text: root.phaseLabel
                    color: root.theme.accentBright
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: root.theme.quietBorder
            opacity: 0.72
        }

        Column {
            width: parent.width
            spacing: 1

            Repeater {
                model: root.activityAttachedFiles.slice(
                    0,
                    root.visibleAttachmentCount
                )

                delegate: RunActivityRow {
                    required property var modelData
                    required property int index

                    theme: root.theme
                    label: root.compactSourcePath(modelData)
                    detail: String(modelData.fileStatus || "available")
                        === "available"
                        ? "Attached file"
                        : "Unavailable"
                    rowState: String(modelData.fileStatus || "available")
                        === "available"
                        ? "complete"
                        : "warning"
                    entranceOrder: index
                }
            }

            RunActivityRow {
                theme: root.theme
                shown: root.activityAttachedFiles.length
                    > root.visibleAttachmentCount
                label: "+" + (
                    root.activityAttachedFiles.length
                    - root.visibleAttachmentCount
                ) + " more attached "
                    + (root.activityAttachedFiles.length
                        - root.visibleAttachmentCount === 1
                        ? "file"
                        : "files")
                detail: "Included in this Run"
                rowState: "quiet"
                entranceOrder: root.visibleAttachmentCount
            }

            RunActivityRow {
                theme: root.theme
                shown: Boolean(root.runActivity.retrievalStarted)
                label: Boolean(root.runActivity.retrievalComplete)
                    ? root.retrievedSourceCount > 0
                        ? "Found " + root.retrievedSourceCount
                            + " Library "
                            + (root.retrievedSourceCount === 1
                                ? "passage"
                                : "passages")
                        : "No matching Library passages"
                    : "Searching Library"
                detail: Boolean(root.runActivity.retrievalComplete)
                    ? root.retrievedSourceCount > 0
                        ? "Across " + root.retrievedFileCount + " "
                            + (root.retrievedFileCount === 1
                                ? "file"
                                : "files")
                        : "Using Chat context"
                    : "Scanning indexed lore"
                rowState: Boolean(root.runActivity.retrievalComplete)
                    ? "complete"
                    : "active"
                entranceOrder: root.visibleAttachmentCount + 1
            }

            Repeater {
                model: Boolean(root.runActivity.retrievalComplete)
                    ? root.retrievedFiles.slice(
                        0,
                        root.visibleRetrievedCount
                    )
                    : []

                delegate: RunActivityRow {
                    required property var modelData
                    required property int index

                    theme: root.theme
                    label: String(modelData.label || "Library file")
                    detail: Number(modelData.passageCount || 0) + " "
                        + (Number(modelData.passageCount || 0) === 1
                            ? "passage"
                            : "passages")
                    rowState: "evidence"
                    entranceOrder: root.visibleAttachmentCount + index + 2
                }
            }

            RunActivityRow {
                theme: root.theme
                shown: root.retrievedFiles.length
                    > root.visibleRetrievedCount
                label: "+" + (
                    root.retrievedFiles.length
                    - root.visibleRetrievedCount
                ) + " more Library "
                    + (root.retrievedFiles.length
                        - root.visibleRetrievedCount === 1
                        ? "file"
                        : "files")
                detail: "Available in Context"
                rowState: "quiet"
                entranceOrder: root.visibleAttachmentCount
                    + root.visibleRetrievedCount
                    + 2
            }

            RunActivityRow {
                theme: root.theme
                shown: Boolean(root.runActivity.contextStarted)
                label: Boolean(root.runActivity.contextComplete)
                    ? "Context compiled"
                    : "Compiling context"
                detail: Boolean(root.runActivity.contextComplete)
                    ? Number(root.runActivity.includedMessageCount || 0)
                        + " "
                        + (Number(
                            root.runActivity.includedMessageCount || 0
                        ) === 1
                            ? "message"
                            : "messages")
                        + " · " + root.formattedTokenCount(
                            root.runActivity.estimatedInputTokens
                        ) + " tokens"
                    : "Selecting relevant evidence"
                rowState: Boolean(root.runActivity.contextComplete)
                    ? "complete"
                    : "active"
                entranceOrder: root.visibleAttachmentCount
                    + root.visibleRetrievedCount
                    + 3
            }

            RunActivityRow {
                theme: root.theme
                shown: root.warningCount > 0
                label: root.warningCount + " context "
                    + (root.warningCount === 1 ? "warning" : "warnings")
                detail: "Review in Context"
                rowState: "warning"
                entranceOrder: root.visibleAttachmentCount
                    + root.visibleRetrievedCount
                    + 4
            }

            RunActivityRow {
                theme: root.theme
                shown: root.toolExecutions.length > root.visibleToolCount
                label: "+" + (
                    root.toolExecutions.length - root.visibleToolCount
                ) + " earlier tool "
                    + (root.toolExecutions.length
                        - root.visibleToolCount === 1
                        ? "call"
                        : "calls")
                detail: "Recorded in Run"
                rowState: "quiet"
                entranceOrder: root.visibleAttachmentCount
                    + root.visibleRetrievedCount
                    + 5
            }

            Repeater {
                model: root.visibleToolExecutions

                delegate: RunActivityRow {
                    required property var modelData
                    required property int index

                    theme: root.theme
                    label: root.toolLabel(modelData)
                    detail: root.toolDetail(modelData)
                    rowState: root.toolRowState(modelData)
                    entranceOrder: root.visibleAttachmentCount
                        + root.visibleRetrievedCount
                        + index
                        + 6
                }
            }

            RunActivityRow {
                theme: root.theme
                shown: Boolean(root.runActivity.modelStarted)
                label: Boolean(root.runActivity.modelOutputStarted)
                    ? "Writing response"
                    : root.toolExecutions.length > 0
                        ? "Reviewing tool results"
                        : "Planning response"
                detail: String(root.runActivity.model || "Model started")
                rowState: root.activeToolPresent ? "quiet" : "active"
                entranceOrder: root.visibleAttachmentCount
                    + root.visibleRetrievedCount
                    + root.visibleToolCount
                    + 6
            }
        }
    }
}
