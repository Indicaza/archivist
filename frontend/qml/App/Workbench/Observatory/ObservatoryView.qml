import QtQuick
import QtWebEngine
import Archivist.Services

// The Observatory dashboard in a panel: the same page as
// `npm run observatory:dashboard`, served by the app's own loopback host.
// The page is opened with this session's control key in its fragment, so
// its Test Lab can run UI tests; no other page gets the key. Links to whole
// log files open in the browser.
Item {
    id: root

    required property var theme
    readonly property string pageUrl: String(IdeServer.observatoryUrl)
    property string statusText: root.pageUrl.length === 0
        ? "The Observatory needs the IDE host, which isn't running. Restart Archivist."
        : "Loading Observatory…"
    property bool loaded: false

    function insidePage(url) {
        return root.pageUrl.length > 0 && String(url).indexOf(root.pageUrl) === 0
    }

    WebEngineView {
        id: webView

        objectName: "observatory.webView"
        anchors.fill: parent
        // The fragment never reaches a server; the page keeps the key for its
        // own same-origin control requests (data.js).
        url: root.pageUrl.length > 0 ? root.pageUrl + "#control=" + IdeServer.observatoryControlToken : "about:blank"
        backgroundColor: root.theme.workspaceBg
        zoomFactor: root.theme.uiScale
        visible: root.loaded

        settings.javascriptCanOpenWindows: false
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false
        // Copy for AI writes a report to the clipboard when a button is pressed.
        settings.javascriptCanAccessClipboard: true

        // Stays on the dashboard; nothing else loads in this panel.
        onNavigationRequested: function(request) {
            if (root.insidePage(request.url) || String(request.url) === "about:blank") {
                request.accept()
            } else {
                request.reject()
            }
        }

        // "Open full build.log" and similar: the browser shows the raw file.
        onNewWindowRequested: function(request) {
            if (root.insidePage(request.requestedUrl)) {
                Qt.openUrlExternally(request.requestedUrl)
            }
        }

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                root.loaded = true
            } else if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                root.loaded = false
                root.statusText = "The Observatory failed to load: " + loadRequest.errorString
            }
        }
    }

    Text {
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 40)
        visible: !root.loaded
        text: root.statusText
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textControlSize
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }
}
