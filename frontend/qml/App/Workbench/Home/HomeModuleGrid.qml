import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0

// Places Home's modules: in order, full width or half (two halves side by
// side when there's room). Positions are computed here and eased, so
// reordering, hiding and resizing in Edit Home glide instead of jumping.
//
// Each module's content is picked by its type id. A plugin will register a
// type with HomeStore and supply its compact presentation the same way.
Item {
    id: root

    required property var theme
    required property var home
    // HomeStore.modules entries to show, in order.
    property var modules: []
    property bool editing: false

    readonly property bool twoColumns: width >= 760
    readonly property real gap: 40
    readonly property real columnGap: 28
    property var heights: ({})
    readonly property var placement: computePlacement()

    implicitHeight: placement.height

    function computePlacement() {
        var heightsNow = heights
        var result = { height: 0, items: {} }
        var y = 0
        var index = 0
        var list = modules
        while (index < list.length) {
            var module = list[index]
            var half = twoColumns && module.span === "half" && module.visible
            var next = index + 1 < list.length ? list[index + 1] : null
            var pair = half && next && next.span === "half" && next.visible
            if (pair) {
                var width = (root.width - columnGap) / 2
                var rowHeight = Math.max(heightsNow[module.id] || 0, heightsNow[next.id] || 0)
                result.items[module.id] = { x: 0, y: y, width: width }
                result.items[next.id] = { x: width + columnGap, y: y, width: width }
                y += rowHeight + gap
                index += 2
            } else {
                result.items[module.id] = { x: 0, y: y, width: root.width }
                y += (heightsNow[module.id] || 0) + gap
                index += 1
            }
        }
        result.height = Math.max(0, y - gap)
        return result
    }

    function noteHeight(moduleId, value) {
        if (heights[moduleId] === value) {
            return
        }
        var next = Object.assign({}, heights)
        next[moduleId] = value
        heights = next
    }

    function indexInStore(moduleId) {
        var list = HomeStore.modules || []
        for (var index = 0; index < list.length; index += 1) {
            if (list[index].id === moduleId) {
                return index
            }
        }
        return -1
    }

    HomeKeyedModel {
        id: moduleModel

        items: root.modules
    }

    Component {
        id: resumeComponent

        ResumeModule {
            theme: root.theme
            entries: root.home.resumeEntries
            diveCollectionId: root.home.diveCollectionId
            diveProgress: 1 - root.home.reveal
            onOpenRequested: function(entry) { root.home.openResume(entry) }
            onPinRequested: function(entry) {
                HomeStore.togglePin({
                    kind: "collection",
                    id: entry.collectionId,
                    title: entry.name,
                    subtitle: "Project"
                })
            }
        }
    }

    Component {
        id: pinnedComponent

        PinnedModule {
            theme: root.theme
            home: root.home
            editing: root.editing
            onOpenRequested: function(pin) { root.home.openLink(pin) }
        }
    }

    Component {
        id: activityComponent

        ActivityModule {
            theme: root.theme
            home: root.home
            onOpenRequested: function(event) {
                root.home.openLink(event.target || ({}))
            }
        }
    }

    Component {
        id: layoutsComponent

        LayoutsModule {
            theme: root.theme
            home: root.home
            onApplyRequested: function(layout) { root.home.layoutRequested(String(layout.id)) }
        }
    }

    Component {
        id: unavailableComponent

        Text {
            width: parent ? parent.width : 0
            text: "This module comes from a plugin that isn't installed. It stays here in case it comes back."
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            wrapMode: Text.Wrap
        }
    }

    Repeater {
        model: moduleModel

        delegate: HomeModuleFrame {
            id: frame

            required property string key
            required property int revision

            readonly property var place: root.placement.items[key] || ({ x: 0, y: 0, width: root.width })
            readonly property int storeIndex: { HomeStore.modules; return root.indexInStore(key) }

            module: { revision; return moduleModel.itemFor(key) }
            theme: root.theme
            editing: root.editing
            x: place.x
            y: place.y
            width: place.width
            canMoveEarlier: storeIndex > 0
            canMoveLater: storeIndex >= 0 && storeIndex < (HomeStore.modules || []).length - 1
            note: {
                if (module.typeId === "archivist.home.activity" && HomeStore.unreadActivity > 0) {
                    return HomeStore.unreadActivity + " new"
                }
                if (module.typeId === "archivist.home.pinned" && root.home.livePins.length > 0) {
                    return String(root.home.livePins.length)
                }
                return ""
            }
            onImplicitHeightChanged: root.noteHeight(key, implicitHeight)
            Component.onCompleted: root.noteHeight(key, implicitHeight)
            onMoveRequested: function(step) { HomeStore.moveModule(key, storeIndex + step) }
            onVisibilityToggled: HomeStore.setModuleVisible(key, !(module.visible !== false))
            onSpanToggled: HomeStore.setModuleSpan(key, module.span === "half" ? "full" : "half")

            actions: [
                HomeChip {
                    theme: root.theme
                    implicitHeight: 26
                    visible: frame.module.typeId === "archivist.home.activity" && HomeStore.unreadActivity > 0
                    label: "Mark read"
                    onClicked: HomeStore.markActivityRead()
                },
                HomeChip {
                    theme: root.theme
                    implicitHeight: 26
                    visible: frame.module.typeId === "archivist.home.activity"
                        && HomeStore.activity.length > 0 && HomeStore.unreadActivity === 0
                    label: "Clear"
                    onClicked: HomeStore.clearActivity()
                },
                HomeChip {
                    theme: root.theme
                    implicitHeight: 26
                    visible: frame.module.typeId === "archivist.home.resume" && root.home.projects.length > 0
                    label: "All projects"
                    onClicked: root.home.showAllProjects()
                }
            ]

            Behavior on x {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }
            Behavior on y {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }
            Behavior on width {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }

            Loader {
                width: frame.width
                active: frame.module.visible !== false
                sourceComponent: {
                    if (frame.module.available === false) return unavailableComponent
                    switch (String(frame.module.typeId)) {
                    case "archivist.home.resume": return resumeComponent
                    case "archivist.home.pinned": return pinnedComponent
                    case "archivist.home.activity": return activityComponent
                    case "archivist.home.layouts": return layoutsComponent
                    }
                    return unavailableComponent
                }
            }
        }
    }
}
