import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One worktree in the selector list: branch, folder, live status and the
// row's actions. The parent decides what a click does.
Rectangle {
    id: root

    required property var theme
    required property var entry

    signal activated()
    signal removeRequested()
    signal forgetRequested()

    readonly property var status: entry.status || null
    readonly property bool active: entry.active === true
    readonly property bool orphaned: entry.orphaned === true
    readonly property bool missing: entry.exists === false
        || (status !== null && status.missing === true)
    readonly property bool opened: String(entry.libraryId || "").length > 0
        && entry.libraryArchived !== true
    readonly property bool removable: entry.linked === true
        && entry.main !== true
        && entry.anchor !== true
        && !orphaned
    readonly property int unsaved: Number(entry.unsavedDocuments || 0)

    function folderName(path) {
        var parts = String(path || "").split("/")
        return parts.length > 0 ? parts[parts.length - 1] : ""
    }

    function statusText() {
        if (orphaned) {
            return "Folder gone · no longer a worktree"
        }
        if (missing) {
            return "Folder missing"
        }
        if (status === null) {
            return "Checking status…"
        }
        if (status.available !== true) {
            return "Status unavailable"
        }
        if (Number(status.conflicted || 0) > 0) {
            return status.conflicted + " conflicted"
        }

        var parts = []

        if (String(status.operation || "").length > 0) {
            parts.push(String(status.operation) + " in progress")
        }
        if (Number(status.changedCount || 0) > 0) {
            parts.push(status.changedCount + " changed")
        }

        return parts.length > 0 ? parts.join(" · ") : "Clean"
    }

    function statusColor() {
        if (orphaned || missing) {
            return theme.danger
        }
        if (status !== null && Number(status.conflicted || 0) > 0) {
            return theme.danger
        }
        if (entry.dirty === true) {
            return theme.warning
        }
        return theme.mutedText
    }

    implicitHeight: 42
    radius: 5
    color: active
        ? theme.activeBg
        : rowMouse.containsMouse
            ? theme.hoverBg
            : "transparent"

    Accessible.role: Accessible.ListItem
    Accessible.name: String(entry.label || "") + ", " + statusText()

    Rectangle {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: parent.height - 12
        radius: 1.5
        color: root.theme.accent
        visible: root.active
    }

    MouseArea {
        id: rowMouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.active || root.orphaned
            ? Qt.ArrowCursor
            : Qt.PointingHandCursor
        onClicked: {
            if (!root.active && !root.orphaned) {
                root.activated()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 8

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            name: "git-branch"
            tone: root.active ? "accent" : "muted"
            iconSize: 14
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: String(root.entry.label || "Worktree")
                    color: root.opened || root.active
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(11)
                    font.weight: root.active ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }

                Text {
                    visible: root.entry.main === true
                        || root.entry.anchor === true
                    text: "main checkout"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                }

                Text {
                    visible: root.entry.locked === true
                    text: "locked"
                    color: root.theme.warning
                    font.pixelSize: root.theme.typeSize(9)
                }
            }

            Text {
                Layout.fillWidth: true
                text: (root.opened || root.active
                        ? ""
                        : "Not opened · ")
                    + root.statusText()
                    + " · "
                    + root.folderName(root.entry.path)
                color: root.statusColor()
                font.pixelSize: root.theme.typeSize(9)
                elide: Text.ElideMiddle
            }
        }

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: root.entry.running === true
            name: "agent"
            tone: "accent"
            iconSize: 13
            accessibleLabel: "Agent running"

            ToolTip.visible: agentHover.hovered
            ToolTip.text: "An agent is running in this worktree"
            ToolTip.delay: 300

            HoverHandler {
                id: agentHover
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: root.unsaved > 0
            implicitWidth: unsavedText.implicitWidth + 8
            implicitHeight: 15
            radius: 7.5
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.warning

            Text {
                id: unsavedText

                anchors.centerIn: parent
                text: root.unsaved + " unsaved"
                color: root.theme.warning
                font.pixelSize: root.theme.typeSize(8)
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            width: 22
            height: 22
            theme: root.theme
            visible: (root.removable || root.orphaned)
                && (rowMouse.containsMouse || hovered)
            iconName: root.orphaned ? "archive" : "close"
            iconSize: 12
            toolTipText: root.orphaned
                ? "Forget this worktree"
                : "Remove worktree…"
            onClicked: {
                if (root.orphaned) {
                    root.forgetRequested()
                } else {
                    root.removeRequested()
                }
            }
        }
    }
}
