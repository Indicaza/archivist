import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Item {
    id: root

    required property var theme

    signal sourceOpening()

    property string pendingLibraryId: ""
    property string pendingFileId: ""

    readonly property var contextRun: ChatStore.inspectedContext || ({})
    readonly property var compiler: contextRun.compiler || ({})
    readonly property var manifest: contextRun.manifest || ({})
    readonly property var warnings: contextRun.warnings || []
    readonly property var sources: contextRun.sources || []
    readonly property bool hasContext: String(contextRun.id || "").length > 0

    function statusLabel(status) {
        switch (String(status || "")) {
        case "included":
            return "INCLUDED"
        case "truncated":
            return "TRUNCATED"
        case "omitted":
            return "OMITTED"
        case "unavailable":
            return "UNAVAILABLE"
        case "failed":
            return "FAILED"
        default:
            return "UNKNOWN"
        }
    }

    function statusColor(status) {
        switch (String(status || "")) {
        case "included":
            return root.theme.success
        case "truncated":
            return root.theme.warning
        case "omitted":
            return root.theme.mutedText
        case "unavailable":
        case "failed":
            return root.theme.danger
        default:
            return root.theme.mutedText
        }
    }

    function filesContain(fileId) {
        var catalog = LibraryStore.files || []

        for (var index = 0; index < catalog.length; index += 1) {
            if (String(catalog[index].id || "") === String(fileId || "")) {
                return true
            }
        }

        return false
    }

    function tryOpenPendingSource() {
        if (
            root.pendingLibraryId.length === 0
            || root.pendingFileId.length === 0
            || LibraryStore.selectedLibraryId !== root.pendingLibraryId
            || LibraryStore.loadingFiles
            || !root.filesContain(root.pendingFileId)
        ) {
            return
        }

        const fileId = root.pendingFileId
        root.pendingLibraryId = ""
        root.pendingFileId = ""
        LibraryStore.previewFile(fileId)
    }

    function openSource(libraryId, fileId) {
        const normalizedLibraryId = String(libraryId || "")
        const normalizedFileId = String(fileId || "")

        if (normalizedLibraryId.length === 0 || normalizedFileId.length === 0) {
            return
        }

        root.sourceOpening()
        root.pendingLibraryId = normalizedLibraryId
        root.pendingFileId = normalizedFileId

        if (LibraryStore.selectedLibraryId !== normalizedLibraryId) {
            LibraryStore.selectLibrary(normalizedLibraryId)
            return
        }

        root.tryOpenPendingSource()
    }

    Connections {
        target: LibraryStore

        function onFilesChanged() {
            root.tryOpenPendingSource()
        }

        function onLoadingFilesChanged() {
            if (!LibraryStore.loadingFiles) {
                Qt.callLater(root.tryOpenPendingSource)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: 7
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.quietBorder
            visible: root.hasContext

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: String(root.compiler.name || root.compiler.id || "Context Compiler")
                        color: root.theme.appText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: String(root.contextRun.provider || "provider")
                            + "  ·  "
                            + String(root.contextRun.model || "model")
                            + "  ·  v"
                            + String(root.compiler.version || 1)
                        color: root.theme.mutedText
                        font.pixelSize: 8
                        opacity: 0.78
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 2

                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: String(root.manifest.estimatedInputTokens || 0)
                            + " / "
                            + String(
                                Math.max(
                                    0,
                                    Number(root.manifest.totalBudget || 0)
                                        - Number(root.manifest.responseTokenReserve || 0)
                                )
                            )
                        color: root.theme.accentBright
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }

                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: "input tokens"
                        color: root.theme.mutedText
                        font.pixelSize: 7
                        opacity: 0.68
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: ChatStore.loadingContext

            Column {
                anchors.centerIn: parent
                spacing: 8

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: true
                    width: 34
                    height: 34
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Loading context…"
                    color: root.theme.mutedText
                    font.pixelSize: 10
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !ChatStore.loadingContext
                && ChatStore.contextErrorMessage.length > 0

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 290)
                spacing: 8

                Text {
                    width: parent.width
                    text: "Context unavailable"
                    color: root.theme.appText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: ChatStore.contextErrorMessage
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !ChatStore.loadingContext
                && ChatStore.contextErrorMessage.length === 0
                && !root.hasContext

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 290)
                spacing: 8

                Text {
                    width: parent.width
                    text: "Select a response"
                    color: root.theme.appText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: "Use the Context button on an Archivist response to inspect how it was assembled."
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        ScrollView {
            id: contextScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !ChatStore.loadingContext
                && ChatStore.contextErrorMessage.length === 0
                && root.hasContext
            clip: true
            contentWidth: availableWidth

            Column {
                width: contextScroll.availableWidth
                spacing: 10

                Rectangle {
                    width: parent.width
                    height: budgetColumn.implicitHeight + 18
                    radius: 7
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: root.theme.quietBorder

                    Column {
                        id: budgetColumn

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 9
                        spacing: 5

                        Text {
                            text: "CONTEXT ACCOUNTING"
                            color: root.theme.mutedText
                            font.pixelSize: 7
                            font.weight: Font.Bold
                            font.letterSpacing: 0.55
                        }

                        Text {
                            width: parent.width
                            text: String(root.manifest.includedMessageCount || 0)
                                + " messages included  ·  "
                                + String(root.manifest.omittedMessageCount || 0)
                                + " omitted"
                            color: root.theme.appText
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            text: String(root.manifest.responseTokenReserve || 0)
                                + " tokens reserved for the response  ·  "
                                + String(root.manifest.compilationTimeMs || 0)
                                + " ms compile"
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6
                    visible: root.warnings.length > 0

                    Text {
                        text: "WARNINGS  " + String(root.warnings.length)
                        color: root.theme.warning
                        font.pixelSize: 7
                        font.weight: Font.Bold
                        font.letterSpacing: 0.55
                    }

                    Repeater {
                        model: root.warnings

                        delegate: Rectangle {
                            required property var modelData

                            width: parent.width
                            height: warningText.implicitHeight + 16
                            radius: 6
                            color: "#1d1913"
                            border.width: 1
                            border.color: "#4d3d26"

                            Text {
                                id: warningText

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                text: String(modelData)
                                color: root.theme.warning
                                font.pixelSize: 8
                                lineHeight: 1.3
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: "SOURCES  " + String(root.sources.length)
                    color: root.theme.mutedText
                    font.pixelSize: 7
                    font.weight: Font.Bold
                    font.letterSpacing: 0.55
                }

                Text {
                    width: parent.width
                    visible: root.sources.length === 0
                    text: "No Library evidence was used for this response."
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                    opacity: 0.72
                }

                Repeater {
                    model: root.sources

                    delegate: Rectangle {
                        id: sourceCard

                        required property var modelData

                        readonly property var metadata: modelData.metadata || ({})
                        readonly property string statusValue: String(modelData.status || "")
                        readonly property string libraryId: String(metadata.libraryId || "")
                        readonly property string fileId: String(metadata.fileId || "")
                        readonly property string retrievalMode: String(metadata.retrievalMode || "attached")

                        width: parent.width
                        height: sourceColumn.implicitHeight + 18
                        radius: 7
                        color: root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.statusColor(sourceCard.statusValue)

                        Column {
                            id: sourceColumn

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 9
                            spacing: 6

                            RowLayout {
                                width: parent.width
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.label || metadata.relativePath || "Library source")
                                    color: root.theme.appText
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    text: root.statusLabel(sourceCard.statusValue)
                                    color: root.statusColor(sourceCard.statusValue)
                                    font.pixelSize: 7
                                    font.weight: Font.Bold
                                }
                            }

                            Text {
                                width: parent.width
                                text: (sourceCard.retrievalMode === "automatic"
                                    ? (sourceCard.statusValue === "included"
                                        || sourceCard.statusValue === "truncated"
                                        ? "AUTO RETRIEVED  ·  "
                                        : "AUTO CANDIDATE  ·  ")
                                    : "ATTACHED  ·  ")
                                    + String(modelData.includedTokens || 0)
                                    + " included / "
                                    + String(modelData.estimatedTokens || 0)
                                    + " estimated tokens"
                                color: sourceCard.retrievalMode === "automatic"
                                    && (sourceCard.statusValue === "included"
                                        || sourceCard.statusValue === "truncated")
                                    ? root.theme.accentBright
                                    : root.theme.mutedText
                                font.pixelSize: 8
                                font.weight: sourceCard.retrievalMode === "automatic"
                                    ? Font.DemiBold
                                    : Font.Normal
                                wrapMode: Text.Wrap
                            }

                            Text {
                                width: parent.width
                                visible: String(modelData.reason || "").length > 0
                                text: String(modelData.reason || "")
                                color: sourceCard.statusValue === "failed"
                                    || sourceCard.statusValue === "unavailable"
                                    ? root.theme.danger
                                    : root.theme.mutedText
                                font.pixelSize: 8
                                lineHeight: 1.3
                                wrapMode: Text.Wrap
                            }

                            Button {
                                width: 92
                                height: 24
                                text: "Open source"
                                enabled: sourceCard.libraryId.length > 0
                                    && sourceCard.fileId.length > 0
                                hoverEnabled: true
                                padding: 0
                                onClicked: root.openSource(
                                    sourceCard.libraryId,
                                    sourceCard.fileId
                                )

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled && parent.hovered
                                        ? root.theme.accentBright
                                        : root.theme.mutedText
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    opacity: parent.enabled ? 1 : 0.45
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.enabled && parent.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                                    border.width: 1
                                    border.color: root.theme.quietBorder
                                }
                            }
                        }
                    }
                }

                Item {
                    width: 1
                    height: 4
                }
            }
        }
    }
}
