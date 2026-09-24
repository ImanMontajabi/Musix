import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Accent colours, grouped by where they come from. The Material ones are
// seeds: the scheme is derived from each, surfaces included. The palette ones
// are used as their palettes give them, in the variant for the current theme,
// and touch the accent roles only (see AccentPalettes.qml). A swatch shows the
// palette's own colour; Theme moves it only where it would be unreadable.
ColumnLayout {
    id: picker
    objectName: "accentPicker"
    spacing: Theme.space12
    readonly property var seeds: ["","#6750a4","#386a20","#00658e","#8f4c38","#7d5260","#6b5f00"]
    readonly property var names: ["Default","Purple","Green","Blue","Terracotta","Mauve","Olive"]

    component Swatch: AbstractButton {
        id: swatch
        // What is stored when chosen, what is painted, and what it is called.
        property string value: ""
        property color paint: "transparent"
        property string label: ""
        readonly property bool chosen: app.accentColor===value
        width: 44; height: 44
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.role: Accessible.RadioButton
        Accessible.name: label+" accent"
        Accessible.checkable: true; Accessible.checked: chosen
        onClicked: app.accentColor=value
        MTooltip {
            objectName: "accentTip"
            visible: swatch.hovered; delay: 650
            text: swatch.label
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: 40; height: 40; radius: Theme.shapeLargeIncreased
            color: swatch.value ? swatch.paint : Theme.high
            border.width: swatch.value ? 0 : 2
            border.color: Theme.outline
            Rectangle {
                anchors.fill: parent; anchors.margins: -4; radius: Theme.shapeExtraLarge
                color: "transparent"; border.width: 2; border.color: Theme.focusRing
                visible: swatch.visualFocus
            }
            Rectangle {
                anchors.fill: parent; radius: Theme.shapeLargeIncreased
                color: Theme.text
                opacity: swatch.down ? Theme.pressedOpacity : swatch.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            }
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: "check"; size: 20
                visible: swatch.chosen
                ink: swatch.value ? (Theme.luminance(swatch.paint)>0.179?"#000000":"#ffffff") : Theme.text
            }
        }
    }

    SungText { text: "Material"; color: Theme.muted; font.pixelSize: Theme.labelLarge; labelRole: true }
    Flow {
        Layout.fillWidth: true; spacing: Theme.space8
        Repeater {
            model: picker.seeds
            Swatch {
                required property string modelData
                required property int index
                objectName: "accentSeed_"+(modelData ? modelData.slice(1) : "default")
                value: modelData; paint: modelData || "transparent"; label: picker.names[index]
            }
        }
    }
    Repeater {
        model: AccentPalettes.palettes
        ColumnLayout {
            id: group
            required property var modelData
            objectName: "accentPalette_"+modelData.key
            Layout.fillWidth: true; spacing: Theme.space12
            SungText { text: group.modelData.name; color: Theme.muted; font.pixelSize: Theme.labelLarge; labelRole: true; Layout.topMargin: Theme.space4 }
            Flow {
                Layout.fillWidth: true; spacing: Theme.space8
                Repeater {
                    model: group.modelData.accents
                    Swatch {
                        required property var modelData
                        objectName: "accentSeed_"+group.modelData.key+"_"+modelData.key
                        value: group.modelData.key+"/"+modelData.key
                        paint: Theme.dark ? modelData.dark : modelData.light
                        label: group.modelData.name+" "+modelData.name
                    }
                }
            }
        }
    }
}
