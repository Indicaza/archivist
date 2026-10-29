import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "../../Icons"

// One entry in a Gravity Well, drawn as a small sphere. The item is the
// entry's logical slot (its hit box, placed by WellGeometry); `tile` is
// what's drawn and the only part that scales or moves, so the slot under
// the pointer never shifts while the Dock wave plays over it.
Item {
    id: root

    objectName: "dock.well.icon"

    required property var theme
    required property var workspace
    required property var entry
    required property var slot

    // Set by the Well. tileScale and tileShift come from the Dock wave and
    // follow it on a light spring; shown is how visible the Well is (0..1);
    // lift raises the drawn orb as the Well wakes; growDown when the Well
    // sits in the top half (it grows away from the window edge).
    property real tileScale: 1
    property real tileShift: 0
    property real shown: 1
    property real lift: 0
    property bool growDown: false
    property bool vertical: false
    // Tooltips and hover marks only once the Well is engaged.
    property bool engaged: true

    signal menuRequested(var entry, Item source, real x, real y)

    x: slot.x
    y: slot.y
    width: slot.size
    height: slot.size

    // The orb, a little inside its slot so neighbours never touch at rest.
    readonly property real orb: Math.round(width * 0.84)

    readonly property string entryId: String(entry.entryId || "")
    readonly property string entryState: String(entry.state || "minimized")
    readonly property var minimizedIds: entry.minimizedViewIds || []
    readonly property var openIds: entry.openViewIds || []
    readonly property var resource: entry.resource || ({})
    readonly property var info: ({
        viewId: String(minimizedIds[minimizedIds.length - 1] || openIds[0] || entryId),
        typeId: String(entry.typeId || ""),
        title: String(entry.title || ""),
        resource: resource
    })
    readonly property string title: {
        ChatStore.chats
        if (panel && String(entry.title || "").length > 0) {
            return String(entry.title)
        }
        var name = root.workspace.viewTitle(info)
        return name.length > 0 ? name : "Untitled"
    }
    readonly property bool minimized: minimizedIds.length > 0
    // A whole panel put away: its tabs come back together.
    readonly property bool panel: entry.panel === true
    readonly property var tabTitles: entry.viewTitles || []
    readonly property bool open: openIds.length > 0
    readonly property bool focusedHere: {
        Docking.revision
        return openIds.indexOf(Docking.focusedViewId) >= 0
    }
    readonly property bool dirty: {
        Documents.revision
        return resource.kind === "document" && Documents.isDirty(String(resource.id || ""))
    }
    readonly property string stateText: panel
        ? "Panel" + (tabTitles.length > 1 ? " · " + tabTitles.length + " tabs" : "") + " · click to restore"
        : entryState === "minimized"
        ? (minimizedIds.length > 1 ? minimizedIds.length + " minimized · click to restore" : "Minimized · click to restore")
        : entryState === "pinnedOpen"
            ? "Pinned · open"
            : "Pinned · click to open"
    readonly property bool hovered: hit.containsMouse

    // The body's colour, warmed toward the accent when it has focus.
    function lit(base) {
        if (!focusedHere && !hovered) {
            return base
        }
        var amount = hovered ? 0.16 : 0.1
        return Qt.tint(base, Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, amount))
    }

    // The wave as drawn: it follows the pointer on a light spring, and with
    // reduced motion it doesn't play at all.
    property real drawnScale: tileScale
    property real drawnShift: tileShift
    Behavior on drawnScale {
        enabled: !root.theme.reducedMotion
        SpringAnimation { spring: 7; damping: 0.42; mass: 0.8; epsilon: 0.002 }
    }
    Behavior on drawnShift {
        enabled: !root.theme.reducedMotion
        SpringAnimation { spring: 7; damping: 0.42; mass: 0.8; epsilon: 0.1 }
    }

    Accessible.role: Accessible.Button
    Accessible.name: title + ", " + stateText + (dirty ? ", unsaved changes" : "")

    Item {
        id: tile

        objectName: "dock.well.tile"

        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        opacity: root.shown
        scale: root.drawnScale
        // Grows away from the window edge, like a Dock icon.
        transformOrigin: root.vertical ? Item.Center : root.growDown ? Item.Top : Item.Bottom
        transform: Translate {
            x: root.vertical ? 0 : root.drawnShift
            y: root.vertical ? root.drawnShift : (root.growDown ? root.lift : -root.lift)
        }

        // Its shadow on the surface below.
        WellGlow {
            width: root.orb * 1.15
            height: width
            x: (parent.width - width) / 2
            y: (parent.height + root.orb) / 2 - height / 2 + root.orb * 0.02
            color: "#000000"
            strength: root.hovered ? 0.62 : 0.5
            transform: Scale { origin.x: root.orb * 0.575; origin.y: root.orb * 0.575; yScale: 0.26 }
        }

        Item {
            id: sphere

            width: root.orb
            height: root.orb
            anchors.centerIn: parent

            // The body: lit from above, falling into shade underneath.
            Rectangle {
                id: body

                anchors.fill: parent
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: root.lit(root.theme.orbTop) }
                    GradientStop { position: 0.48; color: root.lit(root.theme.orbMiddle) }
                    GradientStop { position: 1; color: root.theme.orbBottom }
                }
            }

            // Volume: a broad soft light on the upper left.
            WellGlow {
                width: root.orb * 0.62
                height: width
                x: root.orb * 0.06
                y: root.orb * 0.02
                color: "#ffffff"
                strength: root.hovered ? 0.15 : 0.1
            }

            // Light bounced back up from below, in the accent.
            WellGlow {
                width: root.orb * 0.56
                height: width
                x: (root.orb - width) / 2
                y: root.orb * 0.62
                color: root.theme.accentBright
                strength: root.focusedHere || root.hovered ? 0.16 : 0.07
                transform: Scale { origin.x: root.orb * 0.28; origin.y: root.orb * 0.28; yScale: 0.5 }
            }

            // The rim: a fine line, brighter where the light falls.
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: root.focusedHere
                    ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, root.hovered ? 0.55 : 0.4)
                    : Qt.rgba(1, 1, 1, root.hovered ? 0.16 : 0.08)

                Behavior on border.color { ColorAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.motionFast } }
            }

            // The specular highlight.
            WellGlow {
                width: root.orb * 0.3
                height: width
                x: root.orb * 0.2
                y: root.orb * 0.1
                color: "#ffffff"
                strength: 0.42
                core: 0.12
                transform: Scale { origin.x: root.orb * 0.15; origin.y: root.orb * 0.15; yScale: 0.7 }
            }

            AppIcon {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: root.orb * 0.02
                name: root.workspace.viewIconName(root.info)
                tone: root.hovered || root.focusedHere ? "accent" : root.entryState === "pinnedClosed" ? "muted" : "normal"
                iconSize: 15
                opacity: root.entryState === "pinnedClosed" && !root.hovered ? 0.7 : 0.95
                accessibleLabel: root.title
            }

            // A panel of several tabs: how many, on its shoulder.
            Rectangle {
                visible: root.panel && root.tabTitles.length > 1
                x: root.orb * 0.68
                y: root.orb * 0.66
                width: Math.max(height, countText.implicitWidth + root.theme.px(5))
                height: root.theme.px(12)
                radius: height / 2
                color: root.theme.orbBottom
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)

                Text {
                    id: countText

                    anchors.centerIn: parent
                    text: String(root.tabTitles.length)
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.px(8)
                }
            }

            // Unsaved changes: a small mark, as on its tab.
            Rectangle {
                visible: root.dirty
                x: root.orb * 0.74
                y: root.orb * 0.08
                width: root.theme.px(6)
                height: width
                radius: width / 2
                color: root.theme.warning
                border.width: 1
                border.color: root.theme.orbBottom
            }
        }

        // Under the orb: a filled dot when it's open somewhere, a ring when
        // it's minimized (put away, still running), nothing when it's only
        // pinned.
        Rectangle {
            visible: root.open || root.minimized
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.growDown ? (parent.height - root.orb) / 2 - height - root.theme.px(2) : (parent.height + root.orb) / 2 + root.theme.px(2)
            width: root.theme.px(4)
            height: width
            radius: width / 2
            color: root.open ? (root.focusedHere ? root.theme.accentBright : root.theme.mutedText) : "transparent"
            border.width: root.open ? 0 : 1
            border.color: root.theme.mutedText
        }
    }

    MouseArea {
        id: hit

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.menuRequested(root.entry, root, mouse.x, mouse.y)
                return
            }
            Docking.activateWellEntry(root.entryId)
        }
    }

    ToolTip.visible: hit.containsMouse && !hit.pressed && root.engaged
    ToolTip.delay: 350
    ToolTip.text: root.title + "\n" + root.stateText
        + (root.panel && root.tabTitles.length > 1 ? "\n" + root.tabTitles.join(", ") : "")
}
