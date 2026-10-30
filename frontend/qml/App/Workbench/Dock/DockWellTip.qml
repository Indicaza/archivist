import QtQuick
import "../../Icons"
import "WellTip.js" as WellTip

// The card a Gravity Well sphere shows on hover: what it holds, from what
// the workspace already records (WellTip.describe). One Magnet: its name,
// its type and where it lives. An Anchor: its name and its Magnets in tab
// order, the active one marked. Informational only: it takes no input, so
// clicks and drags reach the Well as before.
//
// The Well places it (x, y, side, pointer) and says how tall it may get
// (`available`); the card picks how many rows fit and says how many more
// there are. Its rows are made once and reused as the pointer moves from
// sphere to sphere.
Item {
    id: root

    objectName: "dock.well.tip"

    required property var theme
    // WellTip.describe() of the entry shown.
    property var card: ({ anchor: false, title: "", kind: "", path: "", dirty: false, note: "", rows: [], count: 0 })
    // Which side of the sphere it's on, and where along that edge the
    // sphere is (its little pointer).
    property string side: "above"
    property real pointer: width / 2
    property real available: 400
    // Most rows it lists before "N more".
    readonly property int cap: 8

    readonly property bool anchorCard: card.anchor === true
    readonly property var rows: card.rows || []
    readonly property real pad: theme.px(10)
    readonly property real rowHeight: theme.px(22)
    readonly property var fitted: WellTip.fit(rows.length, available, header.height + pad * 2 + theme.px(9), rowHeight, cap)

    readonly property real minWidth: theme.px(168)
    readonly property real maxWidth: theme.px(300)
    readonly property real contentWidth: Math.max(header.implicitWidth, list.implicitWidth)

    width: Math.round(Math.max(minWidth, Math.min(maxWidth, contentWidth + pad * 2)))
    height: Math.round(body.height + pad * 2)

    // Never in the way of the pointer: everything under it still gets it.
    enabled: false

    Accessible.role: Accessible.ToolTip
    Accessible.name: card.title + (anchorCard ? ", " + card.note : card.kind.length > 0 ? ", " + card.kind : "")

    // A soft two-step shadow, so it reads over a busy editor.
    Rectangle {
        anchors.fill: frame
        anchors.topMargin: root.theme.px(3)
        anchors.bottomMargin: -root.theme.px(7)
        anchors.leftMargin: root.theme.px(1)
        anchors.rightMargin: -root.theme.px(1)
        radius: frame.radius + 2
        color: "#000000"
        opacity: 0.22
    }
    Rectangle {
        anchors.fill: frame
        anchors.topMargin: root.theme.px(1)
        anchors.bottomMargin: -root.theme.px(2)
        radius: frame.radius + 1
        color: "#000000"
        opacity: 0.3
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: root.theme.radiusSmall + 2
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder

        // A faint light along the top edge, as on the composer.
        Rectangle {
            x: frame.radius
            y: 1
            width: parent.width - frame.radius * 2
            height: 1
            color: root.theme.compactComposerHighlight
        }
    }

    // The little point toward the sphere: a square turned on its corner,
    // half tucked under the card.
    Rectangle {
        readonly property bool across: root.side === "above" || root.side === "below"
        readonly property real size: root.theme.px(8)

        width: size
        height: size
        rotation: 45
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        z: -0.5
        x: across ? root.pointer - size / 2 : root.side === "right" ? -size / 2 : root.width - size / 2
        y: across ? (root.side === "above" ? root.height - size / 2 : -size / 2) : root.pointer - size / 2
    }
    // Covers the inner half of the point, so it joins the card seamlessly.
    Rectangle {
        readonly property bool across: root.side === "above" || root.side === "below"
        readonly property real size: root.theme.px(10)

        width: across ? size * 1.4 : size / 2
        height: across ? size / 2 : size * 1.4
        color: root.theme.surfaceBg
        x: across ? root.pointer - width / 2 : root.side === "right" ? 1 : root.width - width - 1
        y: across ? (root.side === "above" ? root.height - height - 1 : 1) : root.pointer - height / 2
    }

    Column {
        id: body

        x: root.pad
        y: root.pad
        width: root.width - root.pad * 2
        spacing: root.theme.px(8)

        Column {
            id: header

            readonly property real implicitWidth: Math.max(titleRow.implicitWidth, kindLine.visible ? kindLine.x + kindLine.implicitWidth : 0,
                pathLine.visible ? pathLine.x + Math.min(pathLine.implicitWidth, root.theme.px(260)) : 0)

            width: parent.width
            spacing: root.theme.px(2)

            Row {
                id: titleRow

                readonly property real implicitWidth: (cardIcon.visible ? cardIcon.width + spacing : 0) + titleText.implicitWidth
                    + (dirtyMark.visible ? dirtyMark.width + spacing : 0) + (countText.visible ? countText.implicitWidth + spacing * 2 : 0)

                width: parent.width
                spacing: root.theme.px(6)

                AppIcon {
                    id: cardIcon

                    anchors.verticalCenter: parent.verticalCenter
                    name: String(root.card.icon || "file")
                    tone: "muted"
                    iconSize: 13
                }

                Text {
                    id: titleText

                    width: Math.min(implicitWidth, parent.width - (cardIcon.visible ? cardIcon.width + parent.spacing : 0)
                        - (dirtyMark.visible ? dirtyMark.width + parent.spacing : 0)
                        - (countText.visible ? countText.implicitWidth + parent.spacing * 2 : 0))
                    text: root.card.title
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.typeLabel
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                // Unsaved changes, as on its tab.
                Rectangle {
                    id: dirtyMark

                    visible: !root.anchorCard && root.card.dirty === true
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.theme.px(6)
                    height: width
                    radius: width / 2
                    color: root.theme.warning
                }

                Item {
                    visible: countText.visible
                    width: Math.max(0, parent.width - x - countText.implicitWidth)
                    height: 1
                }

                Text {
                    id: countText

                    visible: root.anchorCard
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.card.note
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.typeCaption
                }
            }

            // One Magnet: its type, and how it's kept when that's news.
            Text {
                id: kindLine

                readonly property string line: [root.card.kind, root.card.dirty ? "Unsaved changes" : "", root.card.note]
                    .filter(function(part) { return String(part || "").length > 0 }).join("  ·  ")

                visible: !root.anchorCard && line.length > 0
                x: cardIcon.width + titleRow.spacing
                width: parent.width - x
                text: line
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeCaption
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            // Where it lives, its middle cut when it's long so the folder
            // and the name both show.
            Text {
                id: pathLine

                visible: !root.anchorCard && String(root.card.path || "").length > 0
                x: cardIcon.width + titleRow.spacing
                width: parent.width - x
                text: String(root.card.path || "")
                color: root.theme.mutedText
                opacity: 0.85
                font.family: root.theme.monospaceFontFamily
                font.pixelSize: root.theme.typeMicro + 1
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }
        }

        // An Anchor's Magnets, in tab order.
        Column {
            id: list

            readonly property real implicitWidth: {
                var widest = 0
                for (var index = 0; index < rowsRepeater.count; index += 1) {
                    var row = rowsRepeater.itemAt(index)
                    if (row && row.visible) {
                        widest = Math.max(widest, row.implicitWidth)
                    }
                }
                return Math.min(widest, root.theme.px(280))
            }

            visible: root.anchorCard && root.rows.length > 0
            width: parent.width
            spacing: 0

            Rectangle {
                width: parent.width
                height: 1
                color: root.theme.quietBorder
            }

            Item { width: 1; height: root.theme.px(4) }

            Repeater {
                id: rowsRepeater

                // Made once; each shows a row while there's one for it.
                model: root.cap

                delegate: Item {
                    id: row

                    required property int index

                    readonly property var item: root.rows[index] || ({})
                    readonly property bool active: item.active === true
                    readonly property bool missing: item.missing === true
                    readonly property real implicitWidth: rowIcon.width + root.theme.px(14) + rowTitle.implicitWidth
                        + (rowState.visible ? rowState.implicitWidth + root.theme.px(10) : 0)

                    objectName: "dock.well.tip.row"
                    visible: index < root.fitted.shown
                    width: parent.width
                    height: visible ? root.rowHeight : 0

                    // The active tab: a restrained wash, not a highlight.
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -root.theme.px(4)
                        anchors.rightMargin: -root.theme.px(4)
                        radius: root.theme.radiusSmall
                        color: root.theme.accentSoft
                        visible: row.active
                    }

                    AppIcon {
                        id: rowIcon

                        x: root.theme.px(2)
                        anchors.verticalCenter: parent.verticalCenter
                        name: String(row.item.icon || "file")
                        tone: row.active ? "accent" : "muted"
                        iconSize: 12
                        opacity: row.missing ? 0.5 : 1
                    }

                    Text {
                        id: rowTitle

                        x: rowIcon.x + rowIcon.width + root.theme.px(8)
                        width: Math.max(0, Math.min(implicitWidth, parent.width - x - (rowState.visible ? rowState.implicitWidth + root.theme.px(10) : 0)))
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(row.item.title || "")
                        color: row.missing ? root.theme.mutedText : root.theme.appText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.typeCaption
                        font.italic: row.missing
                        font.weight: row.active ? Font.Medium : Font.Normal
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Row {
                        id: rowState

                        readonly property real implicitWidth: (dot.visible ? dot.width + spacing : 0) + stateText.implicitWidth

                        visible: stateText.text.length > 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: root.theme.px(4)

                        Rectangle {
                            id: dot

                            visible: row.item.dirty === true
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.theme.px(5)
                            height: width
                            radius: width / 2
                            color: root.theme.warning
                        }

                        Text {
                            id: stateText

                            text: row.item.dirty === true ? "unsaved" : row.active ? "active" : ""
                            color: row.item.dirty === true ? root.theme.warning : root.theme.accentBright
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.typeMicro
                        }
                    }
                }
            }

            // The rest, counted.
            Text {
                objectName: "dock.well.tip.more"

                visible: root.fitted.more > 0
                height: visible ? root.rowHeight : 0
                verticalAlignment: Text.AlignVCenter
                x: root.theme.px(22)
                text: root.fitted.more === 1 ? "1 more" : root.fitted.more + " more"
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeCaption
            }
        }
    }
}
