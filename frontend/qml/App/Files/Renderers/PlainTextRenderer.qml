import QtQuick
import QtQuick.Controls

ScrollView {
    id: root

    required property var theme
    property string content: ""
    property bool showBorder: false
    property real pendingContentX: 0
    property real pendingContentY: 0
    property real pendingXRatio: -1
    property real pendingYRatio: -1
    property int restorePass: 0
    property bool restoringViewport: false

    readonly property var flickable: root.contentItem
    readonly property real viewportContentX: root.flickable
        && root.flickable.contentX !== undefined
        ? Number(root.flickable.contentX)
        : 0
    readonly property real viewportContentY: root.flickable
        && root.flickable.contentY !== undefined
        ? Number(root.flickable.contentY)
        : 0
    readonly property real viewportMaximumX: root.flickable
        && root.flickable.contentWidth !== undefined
        ? Math.max(0, Number(root.flickable.contentWidth) - Number(root.flickable.width))
        : 0
    readonly property real viewportMaximumY: root.flickable
        && root.flickable.contentHeight !== undefined
        ? Math.max(0, Number(root.flickable.contentHeight) - Number(root.flickable.height))
        : 0
    readonly property real viewportXRatio: root.viewportMaximumX > 0
        ? Math.max(0, Math.min(1, root.viewportContentX / root.viewportMaximumX))
        : 0
    readonly property real viewportYRatio: root.viewportMaximumY > 0
        ? Math.max(0, Math.min(1, root.viewportContentY / root.viewportMaximumY))
        : 0

    signal viewportChanged(
        real contentX,
        real contentY,
        real xRatio,
        real yRatio
    )

    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    function emitViewportChanged() {
        root.viewportChanged(
            root.viewportContentX,
            root.viewportContentY,
            root.viewportXRatio,
            root.viewportYRatio
        )
    }

    function restoreViewport(contentX, contentY, xRatio, yRatio) {
        root.pendingContentX = Math.max(0, Number(contentX || 0))
        root.pendingContentY = Math.max(0, Number(contentY || 0))
        root.pendingXRatio = Number(xRatio)
        root.pendingYRatio = Number(yRatio)
        root.restorePass = 0
        root.restoringViewport = true
        viewportRestoreTimer.restart()
    }

    function applyPendingViewport() {
        var target = root.flickable

        if (
            !target
            || target.contentX === undefined
            || target.contentY === undefined
        ) {
            root.restorePass += 1

            if (root.restorePass < 30) {
                viewportRestoreTimer.restart()
            } else {
                root.restoringViewport = false
            }
            return
        }

        var maximumX = root.viewportMaximumX
        var maximumY = root.viewportMaximumY
        var targetX = root.pendingXRatio >= 0 && maximumX > 0
            ? root.pendingXRatio * maximumX
            : root.pendingContentX
        var targetY = root.pendingYRatio >= 0 && maximumY > 0
            ? root.pendingYRatio * maximumY
            : root.pendingContentY

        target.contentX = Math.max(0, Math.min(maximumX, targetX))
        target.contentY = Math.max(0, Math.min(maximumY, targetY))

        root.restorePass += 1

        if (root.restorePass < 14) {
            viewportRestoreTimer.restart()
        } else {
            root.restoringViewport = false
            root.emitViewportChanged()
        }
    }

    Connections {
        target: root.flickable
        ignoreUnknownSignals: true

        function onContentXChanged() {
            if (!root.restoringViewport) {
                viewportSaveTimer.restart()
            }
        }

        function onContentYChanged() {
            if (!root.restoringViewport) {
                viewportSaveTimer.restart()
            }
        }

        function onContentWidthChanged() {
            if (root.restoringViewport) {
                viewportRestoreTimer.restart()
            }
        }

        function onContentHeightChanged() {
            if (root.restoringViewport) {
                viewportRestoreTimer.restart()
            }
        }

        function onWidthChanged() {
            if (root.restoringViewport) {
                viewportRestoreTimer.restart()
            }
        }

        function onHeightChanged() {
            if (root.restoringViewport) {
                viewportRestoreTimer.restart()
            }
        }

        function onMovementStarted() {
            root.restoringViewport = false
        }

        function onMovementEnded() {
            root.emitViewportChanged()
        }
    }

    Timer {
        id: viewportSaveTimer

        interval: 32
        repeat: false
        onTriggered: root.emitViewportChanged()
    }

    Timer {
        id: viewportRestoreTimer

        interval: 34
        repeat: false
        onTriggered: root.applyPendingViewport()
    }

    TextArea {
        width: Math.max(root.availableWidth, implicitWidth)
        text: root.content
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.NoWrap
        textFormat: TextEdit.PlainText
        color: root.theme.appText
        selectionColor: root.theme.messageSelectionBg
        selectedTextColor: root.theme.messageSelectionText
        font.family: root.theme.monospaceFontFamily
        font.pixelSize: root.theme.typeCode
        leftPadding: 18
        rightPadding: 18
        topPadding: 16
        bottomPadding: 16

        background: Rectangle {
            color: "transparent"
            border.width: root.showBorder ? 1 : 0
            border.color: root.theme.quietBorder
        }
    }
}
