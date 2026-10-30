import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// The item list of a shelf (a switchable panel). Picking an item only changes
// which of the panel's views is shown; every view keeps its own state, so a
// terminal keeps running and an unsaved file stays unsaved.
//
// Keys: Up/Down move, Return shows, Alt+Up/Down reorder, Delete removes,
// Escape closes.
Popup {
    id: root

    required property var theme
    required property var workspace
    required property string anchorId
    required property var viewIds
    required property string activeViewId
    property bool contentLocked: false

    property int currentIndex: 0
    property bool addingItems: false

    signal showAsTabsRequested()

    parent: Overlay.overlay
    width: 300
    padding: 5
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function openBelow(item) {
        var point = item.mapToItem(null, 0, item.height + 4)
        var room = root.parent ? root.parent.width : point.x + root.width
        root.x = Math.max(4, Math.min(point.x, room - root.width - 4))
        root.y = point.y
        root.open()
    }

    function info(viewId) {
        Docking.revision
        return Docking.view(viewId)
    }

    function show(index) {
        if (index < 0 || index >= root.viewIds.length) {
            return
        }
        Docking.activateView(root.viewIds[index])
        root.close()
    }

    function reorder(index, delta) {
        var target = index + delta
        if (index < 0 || target < 0 || target >= root.viewIds.length) {
            return
        }
        if (Docking.moveView(root.viewIds[index], root.anchorId, target)) {
            root.currentIndex = target
        }
    }

    function remove(index) {
        if (index < 0 || index >= root.viewIds.length) {
            return
        }
        root.workspace.closeView(root.viewIds[index])
        root.currentIndex = Math.max(0, Math.min(root.currentIndex, root.viewIds.length - 2))
    }

    onAboutToShow: {
        root.addingItems = false
        root.currentIndex = Math.max(0, root.viewIds.indexOf(root.activeViewId))
        list.forceActiveFocus()
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.reducedMotion ? 0 : root.theme.motionFast; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: root.theme.reducedMotion ? 0 : 90 }
    }

    background: Rectangle {
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        radius: root.theme.radiusSmall + 2

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.bottomMargin: -8
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            radius: parent.radius + 2
            color: "#000000"
            opacity: 0.3
        }
    }

    component Row_ : Rectangle {
        id: row

        property string iconName: ""
        property string label: ""
        property string detail: ""
        property bool current: false
        property bool checked: false
        property bool dim: false
        signal activated()

        width: parent ? parent.width : 0
        height: 28
        radius: root.theme.radiusSmall
        color: current || rowHover.hovered ? root.theme.hoverBg : "transparent"

        HoverHandler { id: rowHover }
        TapHandler { onTapped: row.activated() }

        AppIcon {
            id: rowIcon
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: row.iconName.length > 0
            name: row.iconName
            tone: row.dim ? "muted" : "normal"
            iconSize: 13
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 29
            anchors.right: rowDetail.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            elide: Text.ElideRight
            color: row.dim ? root.theme.mutedText : root.theme.appText
            font.pixelSize: root.theme.textControlSize
            font.weight: row.checked ? root.theme.textWeightEmphasis : Font.Normal
        }

        Text {
            id: rowDetail
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 110)
            text: row.detail
            elide: Text.ElideMiddle
            color: root.theme.mutedText
            font.pixelSize: root.theme.textMetadataSize
        }
    }

    contentItem: Column {
        spacing: 2

        ListView {
            id: list

            width: parent.width
            height: Math.min(contentHeight, 28 * 9)
            clip: true
            visible: !root.addingItems
            interactive: contentHeight > height
            model: root.viewIds
            currentIndex: root.currentIndex
            keyNavigationEnabled: false
            Accessible.role: Accessible.List
            Accessible.name: "Shelf items"

            Keys.onPressed: function(event) {
                var count = root.viewIds.length
                if (event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                    root.reorder(root.currentIndex, -1)
                } else if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                    root.reorder(root.currentIndex, 1)
                } else if (event.key === Qt.Key_Up) {
                    root.currentIndex = (root.currentIndex - 1 + count) % Math.max(1, count)
                } else if (event.key === Qt.Key_Down) {
                    root.currentIndex = (root.currentIndex + 1) % Math.max(1, count)
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    root.show(root.currentIndex)
                } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    root.remove(root.currentIndex)
                } else {
                    return
                }
                event.accepted = true
            }

            delegate: Row_ {
                id: item

                required property string modelData
                required property int index
                readonly property var view: root.info(modelData)

                width: list.width
                iconName: root.workspace.viewIconName(view)
                label: root.workspace.viewTitle(view)
                detail: root.workspace.libraryLabel(String((view.resource || {}).libraryId || ""))
                checked: modelData === root.activeViewId
                current: index === root.currentIndex
                onActivated: root.show(index)
                Accessible.role: Accessible.ListItem
                Accessible.name: label + (checked ? ", shown" : "")

                // Reorder and remove, shown on hover so the list stays quiet.
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: itemHover.hovered
                    spacing: 0

                    Repeater {
                        model: [
                            { icon: "chevron-up", tip: "Move up", delta: -1 },
                            { icon: "chevron-down", tip: "Move down", delta: 1 },
                            { icon: "close", tip: "Remove from shelf", delta: 0 }
                        ]

                        delegate: ToolButton {
                            required property var modelData

                            width: 22
                            height: 22
                            enabled: modelData.delta === 0
                                || (item.index + modelData.delta >= 0 && item.index + modelData.delta < root.viewIds.length)
                            opacity: enabled ? 1 : 0.35
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            ToolTip.text: modelData.tip
                            Accessible.name: modelData.tip
                            onClicked: modelData.delta === 0 ? root.remove(item.index) : root.reorder(item.index, modelData.delta)
                            background: Rectangle {
                                radius: root.theme.radiusSmall
                                color: parent.hovered ? root.theme.activeBg : root.theme.surfaceBg
                            }
                            contentItem: AppIcon {
                                name: modelData.icon
                                tone: "muted"
                                iconSize: 11
                            }
                        }
                    }
                }

                HoverHandler { id: itemHover }
            }
        }

        Text {
            width: parent.width
            visible: !root.addingItems && root.viewIds.length === 0
            topPadding: 6
            bottomPadding: 6
            leftPadding: 8
            text: "Nothing on this shelf yet."
            color: root.theme.mutedText
            font.pixelSize: root.theme.textMetadataSize
        }

        // Items open elsewhere that can join this shelf.
        ListView {
            id: candidates

            readonly property var items: {
                Docking.revision
                return root.addingItems ? root.workspace.shelfCandidates(root.anchorId) : []
            }

            width: parent.width
            height: Math.min(contentHeight, 28 * 9)
            clip: true
            visible: root.addingItems
            model: items

            delegate: Row_ {
                required property var modelData

                width: candidates.width
                iconName: root.workspace.viewIconName(modelData)
                label: root.workspace.viewTitle(modelData)
                detail: root.workspace.libraryLabel(String((modelData.resource || {}).libraryId || ""))
                onActivated: {
                    root.workspace.addToShelf(root.anchorId, modelData.viewId)
                    root.close()
                }
            }
        }

        Text {
            width: parent.width
            visible: root.addingItems && candidates.items.length === 0
            topPadding: 6
            bottomPadding: 6
            leftPadding: 8
            text: "Everything open is already here."
            color: root.theme.mutedText
            font.pixelSize: root.theme.textMetadataSize
        }

        Rectangle {
            width: parent.width - 8
            x: 4
            height: 1
            color: root.theme.quietBorder
        }

        Row_ {
            iconName: root.addingItems ? "chevron-left" : "add"
            label: root.addingItems ? "Back" : "Add Open Item…"
            dim: true
            onActivated: root.addingItems = !root.addingItems
        }

        Row_ {
            visible: !root.addingItems
            iconName: "terminal"
            label: "New Terminal Here"
            dim: true
            onActivated: {
                root.close()
                root.workspace.openTerminal(root.anchorId)
            }
        }

        Row_ {
            visible: !root.addingItems
            iconName: "history"
            label: "Observatory Here"
            dim: true
            onActivated: {
                root.close()
                root.workspace.openObservatory(root.anchorId)
            }
        }

        Row_ {
            visible: !root.addingItems
            iconName: ""
            label: "Show as Tabs"
            dim: true
            onActivated: {
                root.close()
                root.showAsTabsRequested()
            }
        }
    }
}
