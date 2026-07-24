import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property string heading
    required property string description
    required property var items
    required property var selectedIds
    required property var itemLabel
    property bool busy: false
    property bool showDefaultAgent: false
    property var defaultAgentOptions: []
    property string defaultAgentId: ""

    signal itemToggled(string itemId, bool checked)
    signal defaultAgentSelected(string agentId)

    Layout.fillWidth: true
    Layout.preferredHeight: 270
    color: root.theme.controlSurfaceBg
    border.width: 1
    border.color: root.theme.quietBorder
    radius: root.theme.radiusMedium

    function containsId(id) {
        var normalizedId = String(id || "")
        var values = selectedIds || []

        for (var index = 0; index < values.length; index += 1) {
            if (String(values[index]) === normalizedId) {
                return true
            }
        }

        return false
    }

    function defaultAgentIndex() {
        var options = defaultAgentOptions || []

        for (var index = 0; index < options.length; index += 1) {
            if (String(options[index].id || "") === defaultAgentId) {
                return index
            }
        }

        return 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: root.heading + "  " + String((root.selectedIds || []).length)
            color: root.theme.accentBright
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.Bold
            font.letterSpacing: 0.6
        }

        Text {
            Layout.fillWidth: true
            text: root.description
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(8)
            wrapMode: Text.Wrap
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.items

            delegate: CheckBox {
                required property var modelData

                width: ListView.view.width
                height: root.showDefaultAgent ? 32 : 34
                text: String(root.itemLabel(modelData))
                checked: root.containsId(modelData.id)
                enabled: !root.busy
                font.pixelSize: root.theme.typeSize(10)
                onToggled: root.itemToggled(String(modelData.id), checked)
            }
        }

        ComboBox {
            id: defaultAgentField

            Layout.fillWidth: true
            Layout.preferredHeight: root.showDefaultAgent ? 34 : 0
            visible: root.showDefaultAgent
            enabled: !root.busy
            model: root.defaultAgentOptions
            textRole: "name"
            valueRole: "id"
            font.pixelSize: root.theme.typeSize(9)

            Binding {
                target: defaultAgentField
                property: "currentIndex"
                value: root.defaultAgentIndex()
            }

            onActivated: root.defaultAgentSelected(String(currentValue || ""))

            background: Rectangle {
                color: root.theme.surfaceBg
                border.width: 1
                border.color: root.theme.quietBorder
                radius: 4
            }
        }
    }
}
