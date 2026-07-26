import QtQuick
import QtQuick.Controls
import QtWebChannel
import QtWebEngine

Item {
    id: root

    required property var theme
    property string activeSurface: "terminal"
    property string documentId: ""
    property string documentPath: ""
    property string documentLanguage: "plaintext"
    property string documentContent: ""
    property string documentModifiedAt: ""
    property bool documentReadOnly: true
    property string terminalCollectionId: ""
    property string terminalLibraryId: ""
    property string terminalSessionId: "primary"

    signal dirtyStateReported(string documentId, bool dirty)
    signal terminalStateReported(
        string sessionId,
        string state,
        string title,
        string cwd
    )
    signal saveRequested(
        string documentId,
        string content,
        string expectedModifiedAt
    )
    property bool ready: false
    property string statusText: activeSurface === "editor"
        ? "Loading code editor…"
        : "Loading terminal…"
    property url ideUrl: "http://127.0.0.1:3333/ide/"

    readonly property url surfaceUrl:
        String(root.ideUrl)
            + "?surface="
            + encodeURIComponent(root.activeSurface)

    readonly property string themePayload: JSON.stringify({
        appBg: String(theme.appBg),
        appText: String(theme.appText),
        mutedText: String(theme.mutedText),
        accent: String(theme.accent),
        accentBright: String(theme.accentBright),
        surfaceBg: String(theme.surfaceBg),
        controlSurfaceBg: String(theme.controlSurfaceBg),
        workspaceBg: String(theme.workspaceBg),
        panelBorder: String(theme.panelBorder),
        quietBorder: String(theme.quietBorder),
        hoverBg: String(theme.hoverBg),
        activeBg: String(theme.activeBg),
        codeBlockBg: String(theme.codeBlockBg),
        codeBlockText: String(theme.codeBlockText),
        monospaceFontFamily: String(theme.monospaceFontFamily),
        textControlSize: Number(theme.textControlSize)
    })

    readonly property string terminalPayload: JSON.stringify({
        collectionId: String(root.terminalCollectionId || ""),
        libraryId: String(root.terminalLibraryId || ""),
        sessionId: String(root.terminalSessionId || "primary")
    })

    readonly property string documentPayload: JSON.stringify({
        id: String(root.documentId || ""),
        path: String(root.documentPath || ""),
        language: String(
            root.documentLanguage || "plaintext"
        ),
        content: String(root.documentContent || ""),
        modifiedAt: String(root.documentModifiedAt || ""),
        readOnly: Boolean(root.documentReadOnly)
    })

    WebChannel {
        id: channel

        registeredObjects: [bridge]
    }

    QtObject {
        id: bridge

        WebChannel.id: "archivistBridge"

        property string themeJson: root.themePayload
        property string surface: root.activeSurface
        property string documentJson: root.documentPayload
        property string terminalJson: root.terminalPayload
        property string terminalCommandJson: ""
        property string saveResultJson: ""

        function reportReady(version) {
            root.ready = true
            root.statusText =
                String(root.activeSurface) === "editor"
                    ? "Code editor " + String(version || "ready")
                    : "Terminal " + String(version || "ready")
        }

        function reportStatus(message) {
            root.statusText = String(message || "")
        }

        function reportTerminalState(
            sessionId,
            state,
            title,
            cwd
        ) {
            root.terminalStateReported(
                String(sessionId || ""),
                String(state || ""),
                String(title || ""),
                String(cwd || "")
            )
        }

        function reportDirty(documentId, dirty) {
            root.dirtyStateReported(
                String(documentId || ""),
                Boolean(dirty)
            )
        }

        function requestSave(
            documentId,
            content,
            expectedModifiedAt
        ) {
            root.saveRequested(
                String(documentId || ""),
                String(content || ""),
                String(expectedModifiedAt || "")
            )
        }
    }

    function killTerminal(sessionId) {
        bridge.terminalCommandJson = JSON.stringify({
            type: "kill",
            collectionId:
                String(root.terminalCollectionId || ""),
            libraryId:
                String(root.terminalLibraryId || ""),
            sessionId: String(sessionId || ""),
            nonce:
                Date.now().toString()
                + "-"
                + Math.random().toString()
        })
    }

    function completeSave(documentId, preview) {
        bridge.saveResultJson = JSON.stringify({
            documentId: String(documentId || ""),
            ok: true,
            message: "",
            preview: preview || ({})
        })
    }

    function failSave(documentId, message) {
        bridge.saveResultJson = JSON.stringify({
            documentId: String(documentId || ""),
            ok: false,
            message: String(message || "The file could not be saved.")
        })
    }

    WebEngineView {
        id: webView

        anchors.fill: parent
        url: root.surfaceUrl
        webChannel: channel
        backgroundColor: root.theme.workspaceBg
        focus: root.visible
        focusPolicy: Qt.WheelFocus
        activeFocusOnPress: true

        settings.javascriptCanOpenWindows: false
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false

        onVisibleChanged: {
            if (visible) {
                forceActiveFocus()
            }
        }

        onLoadingChanged: function(loadRequest) {
            if (
                loadRequest.status
                    === WebEngineView.LoadStartedStatus
            ) {
                root.ready = false
                return
            }

            if (
                loadRequest.status
                    === WebEngineView.LoadSucceededStatus
            ) {
                forceActiveFocus()
                return
            }

            if (
                loadRequest.status
                    === WebEngineView.LoadFailedStatus
            ) {
                root.ready = false
                root.statusText =
                    "IDE surface failed to load: "
                    + loadRequest.errorString
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.ready
        text: root.statusText
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textControlSize
    }
}
