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
    required property var file
    required property var preview
    required property bool loading
    required property string errorMessage
    property bool dirty: false
    property bool initialDocumentLoaded: false

    signal dirtyStateReported(string documentId, bool dirty)

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

    readonly property string stableDocumentId:
        root.documentIdFor(
            LibraryStore.selectedLibraryId,
            file && file.id ? file.id : ""
        )

    readonly property string documentPath:
        file && (file.relativePath || file.name)
            ? String(file.relativePath || file.name)
            : ""

    readonly property string documentModifiedAt:
        preview
        && preview.file
        && preview.file.modifiedAt
            ? String(preview.file.modifiedAt)
            : file && file.modifiedAt
                ? String(file.modifiedAt)
                : ""

    readonly property bool previewMatchesDocument:
        preview
        && preview.file
        && String(preview.file.id || "")
            === String(file && file.id ? file.id : "")

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
        }
    }

    Component.onCompleted: {
        if (previewMatchesDocument) {
            initialDocumentLoaded = true
        }
    }

    IdeHost {
        id: ideHost

        anchors.fill: parent
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
        documentPath: root.documentPath
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
            ideHost.failSave(
                root.documentIdFor(libraryId, fileId),
                message
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
