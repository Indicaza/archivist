import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "../../Files/Renderers"
import "../IdeHost"

// One Monaco page for one Anchor. Every editor view in the Anchor shows its
// document here; the text lives in the native buffer store (Documents), so
// moving a view to another Anchor or window rebuilds nothing but the page's
// copy, and two views of one document stay in step.
Item {
    id: root

    required property var theme
    required property string anchorId
    // The Anchor's active view when it is an editor, else "".
    property string viewId: ""
    property bool shown: false

    // Unique per page instance: edits this page reports are not echoed back.
    readonly property string hostId: "editor:" + anchorId + ":" + Math.random().toString(36).slice(2, 10)
    property string documentId: ""
    property var openedDocuments: ({})
    property bool markdownDocument: false

    signal fileLocationRequested(string filePath, int lineNumber, int columnNumber)
    signal previewRequested(string viewId)

    readonly property var snapshot: {
        Documents.revision
        return root.documentId.length > 0 ? Documents.snapshot(root.documentId) : ({})
    }
    readonly property bool loaded: Boolean(snapshot.loaded)
    readonly property string loadError: String(snapshot.error || "")

    function libraryRootPath(libraryId) {
        var libraries = LibraryStore.libraries || []
        for (var index = 0; index < libraries.length; index += 1) {
            if (String(libraries[index].id || "") === String(libraryId || "")) {
                return String(libraries[index].rootPath || "")
            }
        }
        return ""
    }

    function absolutePath(rootPath, relativePath) {
        var relative = String(relativePath || "")
        if (relative.length === 0 || rootPath.length === 0) {
            return relative
        }
        if (relative.charAt(0) === "/" || /^[A-Za-z]:[\\/]/.test(relative)) {
            return relative
        }
        return rootPath + (/[\\/]$/.test(rootPath) ? "" : "/") + relative
    }

    // Opens the active view's document in the page with the store's current
    // text and version, as one payload.
    function openActive() {
        var view = root.viewId.length > 0 ? Docking.view(root.viewId) : ({})
        var resource = view.resource || ({})
        var nextDocument = String(resource.id || "")

        root.documentId = nextDocument

        if (!root.shown || nextDocument.length === 0) {
            return
        }

        var current = Documents.snapshot(nextDocument)

        if (!current.loaded) {
            return // documentLoaded will call back
        }

        var details = resource.details || ({})
        var identity = FileIdentity.resolve({
            fileName: String(details.name || details.path || ""),
            extension: String(details.extension || ""),
            mimeType: String(details.mimeType || ""),
            languageId: String(details.languageId || "")
        })
        var rootPath = libraryRootPath(resource.libraryId)
        var state = view.state || ({})

        root.markdownDocument = identity.preferredRendererId === "markdown"

        var opened = root.openedDocuments
        opened[nextDocument] = true
        root.openedDocuments = opened

        ideHost.documentJsonOverride = JSON.stringify({
            id: nextDocument,
            libraryId: String(resource.libraryId || ""),
            path: String(details.path || current.path || ""),
            filePath: absolutePath(rootPath, String(details.path || current.path || "")),
            workspaceRoot: rootPath,
            language: String(identity.languageId || "plaintext"),
            content: String(current.text || ""),
            modifiedAt: String(current.modifiedAt || ""),
            readOnly: Boolean(current.readOnly),
            synced: true,
            version: Number(current.version || 0),
            viewId: root.viewId,
            viewState: String(state.editor || ""),
            nonce: Date.now().toString(36) + Math.random().toString(36).slice(2, 6)
        })

        if (state.reveal) {
            var reveal = state.reveal
            Qt.callLater(function() {
                ideHost.revealLocation(nextDocument, Number(reveal.line || 1), Number(reveal.column || 1))
            })
            Docking.mergeViewState(root.viewId, { reveal: null })
        }
    }

    function resetDocument(documentId) {
        var current = Documents.snapshot(documentId)
        if (!current.loaded) {
            return
        }
        ideHost.sendSyncCommand({
            type: "reset",
            documentId: documentId,
            content: String(current.text || ""),
            version: Number(current.version || 0),
            readOnly: Boolean(current.readOnly)
        })
    }

    // Drops page models for documents no view in this Anchor shows anymore.
    function forgetUnusedDocuments() {
        var anchor = Docking.anchor(root.anchorId)
        var views = anchor.views || []
        var held = ({})

        for (var index = 0; index < views.length; index += 1) {
            var view = Docking.view(views[index])
            if (view.typeId === "archivist.editor" && view.resource) {
                held[String(view.resource.id || "")] = true
            }
        }

        var opened = root.openedDocuments
        var changed = false

        for (var documentId in opened) {
            if (!held[documentId]) {
                ideHost.sendSyncCommand({ type: "forget", documentId: documentId })
                delete opened[documentId]
                changed = true
            }
        }

        if (changed) {
            root.openedDocuments = opened
        }
    }

    function save() {
        if (root.documentId.length > 0) {
            Documents.save(root.documentId)
        }
    }

    onViewIdChanged: openActive()
    onShownChanged: {
        if (shown) {
            openActive()
        }
    }

    Connections {
        target: Docking

        function onRevisionChanged() {
            forgetTimer.restart()
        }
    }

    Timer {
        id: forgetTimer
        interval: 400
        onTriggered: root.forgetUnusedDocuments()
    }

    Connections {
        target: Documents

        function onDocumentLoaded(documentId) {
            if (documentId === root.documentId) {
                root.openActive()
            }
        }

        function onDocumentEdited(documentId, hostId, baseVersion, version, changesJson) {
            if (hostId === root.hostId || !root.openedDocuments[documentId]) {
                return
            }

            var changes = []
            try {
                changes = JSON.parse(changesJson)
            } catch (error) {
                root.resetDocument(documentId)
                return
            }

            ideHost.sendSyncCommand({
                type: "applyEdits",
                documentId: documentId,
                baseVersion: baseVersion,
                version: version,
                changes: changes
            })
        }

        function onDocumentReset(documentId) {
            if (root.openedDocuments[documentId]) {
                root.resetDocument(documentId)
            }
        }

        function onDocumentSaved(documentId) {
            if (root.openedDocuments[documentId]) {
                ideHost.completeSave(documentId, ({}))
            }
        }

        function onSaveFailed(documentId, message) {
            if (root.openedDocuments[documentId]) {
                ideHost.failSave(documentId, message)
            }
        }
    }

    Rectangle {
        id: markdownToolbar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.markdownDocument ? 28 : 0
        visible: root.markdownDocument
        color: root.theme.workspaceBg
        z: 2

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "MARKDOWN SOURCE"
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightStrong
            font.letterSpacing: root.theme.textTrackingLabel
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 72
            height: 22
            text: "PREVIEW"
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered
            ToolTip.text: "Show rendered Markdown in this tab"
            onClicked: root.previewRequested(root.viewId)

            contentItem: Text {
                text: parent.text
                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
                font.weight: root.theme.textWeightStrong
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: 3
                color: parent.hovered ? root.theme.hoverBg : "transparent"
                border.width: 1
                border.color: parent.hovered ? root.theme.panelBorder : root.theme.quietBorder
            }
        }
    }

    // Recovered edits and outside changes are decisions, not surprises.
    property var dismissedRecovery: ({})

    DockNoticeBar {
        id: notice

        readonly property bool diskChanged: Boolean(root.snapshot.diskChanged)
        readonly property bool recovered: Boolean(root.snapshot.recovered)
            && !root.dismissedRecovery[root.documentId]

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: markdownToolbar.visible ? markdownToolbar.bottom : parent.top
        z: 2
        theme: root.theme
        open: root.shown && root.loaded && (diskChanged || recovered)
        tone: diskChanged ? "warning" : "info"
        text: diskChanged
            ? "This file changed on disk while you had unsaved edits."
            : "Unsaved edits recovered from your last session."
        shortText: diskChanged ? "Changed on disk" : "Recovered edits"
        readonly property bool narrow: width < 520
        secondaryText: diskChanged ? (narrow ? "Use Disk" : "Use File on Disk") : "Discard"
        primaryText: diskChanged ? (narrow ? "Keep Mine" : "Keep My Edits") : "Save"
        dismissible: !diskChanged

        onPrimaryClicked: {
            if (diskChanged) {
                Documents.keepUnsavedOverDisk(root.documentId)
            } else {
                Documents.save(root.documentId)
            }
        }
        onSecondaryClicked: {
            if (diskChanged) {
                Documents.takeDiskVersion(root.documentId)
            } else {
                Documents.discard(root.documentId)
            }
        }
        onDismissed: {
            var dismissed = root.dismissedRecovery
            dismissed[root.documentId] = true
            root.dismissedRecovery = dismissed
        }
    }

    IdeHost {
        id: ideHost

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: notice.bottom
        visible: root.shown && root.loaded
        theme: root.theme
        activeSurface: "editor"
        zoom: {
            Docking.revision
            var state = root.viewId.length > 0 ? (Docking.view(root.viewId).state || {}) : {}
            return Number(state.zoom || 1)
        }

        onReadyChanged: {
            if (ready) {
                root.openActive()
            }
        }

        onEditsReported: function(documentId, baseVersion, changesJson, length) {
            var result = Documents.applyEdits(documentId, root.hostId, baseVersion, changesJson)

            if (!result.ok) {
                root.resetDocument(documentId)
                return
            }

            // Guard against the page and the store disagreeing on the text
            // (it would make every later offset wrong).
            var current = Documents.snapshot(documentId)
            if (String(current.text || "").length !== length) {
                root.resetDocument(documentId)
            }
        }

        onResyncRequested: function(documentId) {
            root.resetDocument(documentId)
        }

        onViewStateReported: function(viewId, documentId, stateJson) {
            if (viewId.length > 0 && Docking.view(viewId).viewId) {
                Docking.mergeViewState(viewId, { editor: stateJson })
            }
        }

        onSaveRequested: function(documentId) {
            Documents.save(documentId)
        }

        onFileLocationRequested: function(filePath, lineNumber, columnNumber) {
            root.fileLocationRequested(filePath, lineNumber, columnNumber)
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.shown && root.documentId.length > 0 && !root.loaded && root.loadError.length === 0
        color: root.theme.workspaceBgDeep

        DocumentLoadingState {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 48)
            theme: root.theme
            title: "Opening code editor"
            detail: "Loading the file into its shared buffer."
            fileLabel: String(root.snapshot.path || "")
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 60)
        spacing: 8
        visible: root.shown && root.loadError.length > 0

        Text {
            width: parent.width
            text: "This file could not be opened"
            color: root.theme.danger
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textPanelTitleSize
            font.weight: root.theme.textWeightStrong
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: root.loadError
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
