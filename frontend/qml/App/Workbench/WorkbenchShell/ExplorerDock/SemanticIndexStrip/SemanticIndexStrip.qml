import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

// Per-Library semantic search: consent, indexing progress, errors, and
// pause / resume / cancel. Keyword search never depends on any of this.
Rectangle {
    id: root

    required property var theme
    required property string libraryId

    readonly property var status: {
        SemanticIndex.revision
        return root.libraryId.length > 0 ? SemanticIndex.status(root.libraryId) : ({})
    }
    readonly property var job: root.status.job || ({})
    readonly property string indexState: String(root.status.state || "off")
    readonly property bool enabledForLibrary: root.status.enabled === true
    readonly property int processed: Number(root.job.processedItems || 0)
    readonly property int total: Number(root.job.totalItems || 0)
    readonly property string errorText: {
        if (root.enabledForLibrary && root.status.keyConfigured === false)
            return "Add an OpenAI API key in the Agent editor's Model section to index."
        if (root.indexState === "paused" && root.job.pauseReason === "user")
            return ""
        if (["paused", "failed", "waiting"].indexOf(root.indexState) >= 0)
            return String(root.job.errorMessage || "")
        return ""
    }

    function label() {
        switch (root.indexState) {
        case "indexing":
            return root.total > 0
                ? "Indexing " + root.processed + " / " + root.total
                : "Preparing index"
        case "waiting":
            return "Indexing retries soon"
        case "paused":
            return "Indexing paused"
        case "failed":
            return "Indexing failed"
        case "outdated":
            return Number(root.status.pending || 0) + " chunks to index"
        case "ready":
            return "Semantic: " + Number(root.status.searchable || 0) + " chunks"
        default:
            return "Semantic search off"
        }
    }

    function dotColor() {
        switch (root.indexState) {
        case "ready":
            return root.theme.success
        case "indexing":
        case "waiting":
        case "outdated":
            return root.theme.accent
        case "paused":
            return root.theme.warning
        case "failed":
            return root.theme.danger
        default:
            return root.theme.mutedText
        }
    }

    function estimateText() {
        const tokens = Number(root.status.pendingTokens || 0)
        const dollars = tokens / 1000000 * 0.02
        return "About " + tokens.toLocaleString(Qt.locale(), "f", 0) + " tokens"
            + (tokens > 0 ? " (≈ $" + (dollars < 0.01 ? "0.01 or less" : dollars.toFixed(2)) + ")" : "")
    }

    implicitHeight: content.implicitHeight + 8
    color: root.theme.surfaceBg
    visible: SemanticIndex.ready && root.libraryId.length > 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.quietBorder
        opacity: 0.7
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 7
        spacing: 3

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: root.dotColor()
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

            StripButton {
                text: "Enable…"
                visible: !root.enabledForLibrary
                onClicked: consentDialog.open()
            }

            StripButton {
                text: "Pause"
                visible: root.indexState === "indexing" || root.indexState === "waiting"
                onClicked: SemanticIndex.pause(root.libraryId)
            }

            StripButton {
                text: root.indexState === "failed" ? "Retry"
                    : root.indexState === "outdated" ? "Index now"
                    : "Resume"
                visible: ["paused", "failed", "outdated"].indexOf(root.indexState) >= 0
                onClicked: SemanticIndex.resume(root.libraryId)
            }

            StripButton {
                text: "Cancel"
                visible: root.indexState === "indexing" || root.indexState === "waiting"
                onClicked: SemanticIndex.cancel(root.libraryId)
            }

            StripButton {
                text: "Turn off"
                visible: root.enabledForLibrary
                    && root.indexState !== "indexing"
                    && root.indexState !== "waiting"
                onClicked: disableDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            radius: 1
            visible: root.indexState === "indexing" && root.total > 0
            color: root.theme.quietBorder

            Rectangle {
                width: parent.width * Math.min(1, root.processed / Math.max(1, root.total))
                height: parent.height
                radius: 1
                color: root.theme.accent
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: root.indexState === "failed" ? root.theme.danger : root.theme.warning
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }

    component StripButton: Button {
        id: stripButton

        implicitWidth: buttonLabel.implicitWidth + 12
        implicitHeight: 20
        hoverEnabled: true
        padding: 0

        contentItem: Text {
            id: buttonLabel

            text: stripButton.text
            color: stripButton.hovered ? root.theme.appText : root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightStrong
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 3
            color: stripButton.hovered ? root.theme.hoverBg : "transparent"
        }
    }

    ConfirmDialog {
        id: consentDialog

        objectName: "semanticConsentDialog"
        title: "Turn on semantic search for this Library?"
        confirmText: "Turn on"
        body: "Archivist will send the text of this Library's indexed files to OpenAI ("
            + SemanticIndex.model + ") to create embeddings, using your OpenAI API key. "
            + "The vectors are stored only in Archivist's database on this computer.\n\n"
            + root.estimateText() + " for the first pass; later changes only send edited text. "
            + "Indexing runs in the background and can be paused or cancelled at any time.\n\n"
            + "Keyword search keeps working whether or not this is on."
        onOpened: SemanticIndex.refresh(root.libraryId)
        onAccepted: SemanticIndex.enable(root.libraryId)
    }

    ConfirmDialog {
        id: disableDialog

        title: "Turn off semantic search?"
        confirmText: "Turn off"
        body: "This Library's embeddings are deleted and nothing more is sent to OpenAI. "
            + "Turning it back on indexes the Library again."
        onAccepted: SemanticIndex.disable(root.libraryId)
    }

    component ConfirmDialog: Dialog {
        id: dialog

        property string body
        property string confirmText: "OK"

        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(440, Math.max(300, (parent ? parent.width : 440) - 48))
        x: Math.round(((parent ? parent.width : width) - width) / 2)
        y: Math.round(((parent ? parent.height : height) - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: root.theme.controlSurfaceBg
            radius: 8
            border.width: 1
            border.color: root.theme.panelBorder
        }

        header: Item {}
        footer: Item {}

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                text: dialog.title
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textBodySize
                font.weight: root.theme.textWeightEmphasis
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                text: dialog.body
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                lineHeight: 1.2
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.leftMargin: 20
                Layout.rightMargin: 14
                Layout.bottomMargin: 14
                spacing: 6

                Item {
                    Layout.fillWidth: true
                }

                StripButton {
                    text: "Cancel"
                    implicitHeight: 26
                    onClicked: dialog.reject()
                }

                StripButton {
                    text: dialog.confirmText
                    implicitHeight: 26
                    onClicked: dialog.accept()
                }
            }
        }
    }
}
