import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../../Files/FileIdentity.js" as FileIdentity
import "../../../Files/Renderers"
import "../../IdeHost"

Rectangle {
    id: root

    required property var theme
    required property bool active
    required property bool markdownDocument
    required property var file
    required property var preview
    required property bool loading
    required property string errorMessage
    property bool dirty: false
    property bool initialDocumentLoaded: false

    signal dirtyStateReported(string documentId, bool dirty)
    signal saveFailed(
        string documentId,
        string message
    )
    signal markdownPreviewRequested()
    signal fileLocationRequested(
        string filePath,
        int lineNumber,
        int columnNumber
    )
    signal documentReady(string documentId)

    readonly property string content:
        preview && preview.content
            ? String(preview.content)
            : ""

    readonly property var fileIdentity:
        FileIdentity.resolve({
            fileName:
                file && (file.name || file.relativePath)
                    ? String(
                        file.name || file.relativePath
                    )
                    : "",
            extension:
                file && file.extension
                    ? String(file.extension)
                    : "",
            mimeType:
                file && file.mimeType
                    ? String(file.mimeType)
                    : "",
            languageId:
                file && file.languageId
                    ? String(file.languageId)
                    : ""
        })

    function documentIdFor(libraryId, fileId) {
        return String(CollectionStore.selectedCollectionId || "")
            + ":"
            + String(libraryId || "")
            + ":"
            + String(fileId || "")
    }

    readonly property string documentLibraryId:
        String(LibraryStore.activeFileLibraryId || "")

    readonly property string stableDocumentId:
        root.documentIdFor(
            LibraryStore.activeFileLibraryId,
            file && file.id ? file.id : ""
        )

    readonly property string documentPath:
        file && (file.relativePath || file.name)
            ? String(file.relativePath || file.name)
            : ""

    readonly property string documentWorkspaceRoot:
        LibraryStore.activeFileLibrary
        && LibraryStore.activeFileLibrary.rootPath
            ? String(
                LibraryStore.activeFileLibrary.rootPath
            )
            : ""

    readonly property string documentFilePath: {
        var relativePath = String(root.documentPath || "")
        var workspaceRoot = String(
            root.documentWorkspaceRoot || ""
        )

        if (relativePath.length === 0) {
            return ""
        }

        if (
            relativePath.charAt(0) === "/"
            || /^[A-Za-z]:[\\/]/.test(relativePath)
            || relativePath.indexOf("\\\\") === 0
        ) {
            return relativePath
        }

        if (workspaceRoot.length === 0) {
            return relativePath
        }

        var separator = /[\\/]$/.test(workspaceRoot)
            ? ""
            : "/"

        return workspaceRoot + separator + relativePath
    }

    readonly property string documentModifiedAt:
        preview
        && preview.file
        && preview.file.modifiedAt
            ? String(preview.file.modifiedAt)
            : file && file.modifiedAt
                ? String(file.modifiedAt)
                : ""

    readonly property bool previewMatchesDocument: Boolean(
        preview
        && preview.file
        && String(preview.file.id || "")
            === String(file && file.id ? file.id : "")
    )

    function saveDocument(documentId) {
        ideHost.saveDocument(documentId)
    }

    function discardDocument(documentId) {
        ideHost.discardDocument(documentId)
    }

    function revealLocation(
        documentId,
        lineNumber,
        columnNumber
    ) {
        ideHost.revealLocation(
            documentId,
            lineNumber,
            columnNumber
        )
    }

    readonly property bool blockingInitialLoad:
        root.active
        && root.loading
        && !root.initialDocumentLoaded

    color: root.theme.workspaceBg
    clip: true

    onStableDocumentIdChanged: {
        dirty = false
        initialDocumentLoaded = false
    }

    onPreviewMatchesDocumentChanged: {
        if (previewMatchesDocument) {
            initialDocumentLoaded = true
            documentReady(stableDocumentId)
        }
    }

    Component.onCompleted: {
        if (previewMatchesDocument) {
            initialDocumentLoaded = true
            documentReady(stableDocumentId)
        }
    }

    Rectangle {
        id: markdownToolbar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.markdownDocument ? 30 : 0
        visible: root.markdownDocument
        color: root.theme.workspaceBg
        z: 12

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Text {
                text: "MARKDOWN EDIT"
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize:
                    root.theme.textMetadataSize
                font.weight:
                    root.theme.textWeightStrong
                font.letterSpacing:
                    root.theme.textTrackingLabel
            }
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 72
            height: 24
            text: "PREVIEW"
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered
            ToolTip.text: "Open rendered Markdown"
            onClicked:
                root.markdownPreviewRequested()

            contentItem: Text {
                text: parent.text
                color: parent.hovered
                    ? root.theme.appText
                    : root.theme.mutedText
                font.family:
                    root.theme.bodyFontFamily
                font.pixelSize:
                    root.theme.textMetadataSize
                font.weight:
                    root.theme.textWeightStrong
                horizontalAlignment:
                    Text.AlignHCenter
                verticalAlignment:
                    Text.AlignVCenter
            }

            background: Rectangle {
                radius: 3
                color: parent.hovered
                    ? root.theme.hoverBg
                    : "transparent"
                border.width: 1
                border.color: parent.hovered
                    ? root.theme.panelBorder
                    : root.theme.quietBorder
            }
        }
    }

    IdeHost {
        id: ideHost

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top:
            markdownToolbar.visible
                ? markdownToolbar.bottom
                : parent.top
        visible:
            root.active
            && (
                root.initialDocumentLoaded
                || (
                    !root.loading
                    && root.errorMessage.length === 0
                )
            )
        theme: root.theme
        activeSurface: "editor"
        documentId:
            root.active ? root.stableDocumentId : ""
        documentLibraryId: root.documentLibraryId
        documentPath: root.documentPath
        documentFilePath: root.documentFilePath
        documentWorkspaceRoot:
            root.documentWorkspaceRoot
        documentLanguage:
            String(
                root.fileIdentity.languageId
                || "plaintext"
            )
        documentContent: root.content
        documentModifiedAt: root.documentModifiedAt
        documentReadOnly: false

        onDirtyStateReported: function(documentId, dirty) {
            if (documentId === root.stableDocumentId) {
                root.dirty = dirty
            }

            root.dirtyStateReported(documentId, dirty)
        }

        onFileLocationRequested: function(
            filePath,
            lineNumber,
            columnNumber
        ) {
            root.fileLocationRequested(
                filePath,
                lineNumber,
                columnNumber
            )
        }

        onSaveRequested: function(
            documentId,
            content,
            expectedModifiedAt
        ) {
            if (
                documentId !== root.stableDocumentId
                || !root.active
            ) {
                return
            }

            LibraryStore.saveFileContent(
                String(root.file.id || ""),
                content,
                expectedModifiedAt
            )
        }
    }

    Connections {
        target: LibraryStore

        function onFileSaved(
            libraryId,
            fileId,
            savedPreview
        ) {
            ideHost.completeSave(
                root.documentIdFor(libraryId, fileId),
                savedPreview
            )
        }

        function onFileSaveFailed(
            libraryId,
            fileId,
            message
        ) {
            var documentId = root.documentIdFor(
                libraryId,
                fileId
            )

            ideHost.failSave(documentId, message)
            root.saveFailed(
                documentId,
                String(message || "")
            )
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.blockingInitialLoad
        color: root.theme.workspaceBgDeep
        z: 20

        DocumentLoadingState {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 48)
            theme: root.theme
            title: "Opening code editor"
            detail:
                "Loading the file into its persistent editor model."
            fileLabel: root.documentPath
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 60)
        spacing: 8
        visible:
            !root.initialDocumentLoaded
            && !root.loading
            && root.errorMessage.length > 0
        z: 25

        Text {
            width: parent.width
            text: "Code preview unavailable"
            color: root.theme.danger
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textPanelTitleSize
            font.weight: root.theme.textWeightStrong
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: root.errorMessage
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
