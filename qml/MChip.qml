import QtQuick
import QtQuick.Controls

// Material 3 chip.
//
// Material has four: an assist chip acts, a filter chip turns a view on and
// off and shows a check while it is on, a suggestion chip offers something to
// try, and an input chip stands for something the reader entered and carries
// the means to take it back out. They share one container, at 32dp on an 8dp
// corner with a 48dp target around it.
AbstractButton {
    id: control
    property bool selected: false
    property bool selectable: true
    // "filter", "assist", "suggestion" or "input".
    property string variant: "filter"
    readonly property bool removable: variant === "input"
    // What an input chip's remove asks for.
    signal removed()
    property string symbol: ""
    readonly property bool leads: symbol.length > 0 || (selectable && selected)
    implicitWidth: label.implicitWidth + 24 + (leads ? 26 : 0) + (removable ? 26 : 0)
    implicitHeight: Theme.selectionTarget
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: selectable ? Accessible.CheckBox : Accessible.Button
    Accessible.checkable: selectable
    Accessible.checked: selected
    readonly property bool dimmed: !enabled
    background: Rectangle {
        y: (control.height-Theme.chipHeight)/2; height: Theme.chipHeight; radius: Theme.shapeSmall
        color: control.dimmed && control.selected
                 ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContainerOpacity)
             : control.selected ? Theme.primaryContainer : "transparent"
        border.width: control.selected ? 0 : 1
        border.color: control.dimmed ? Qt.rgba(Theme.text.r,Theme.text.g,Theme.text.b,Theme.disabledContainerOpacity) : Theme.controlOutline
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.selected ? Theme.containerText : Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3; radius: Theme.shapeMedium
            color: "transparent"; border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        opacity: control.dimmed ? Theme.disabledContentOpacity : 1
        Icon {
            x: 12; size: Theme.chipIcon; anchors.verticalCenter: parent.verticalCenter
            besideText: Theme.labelLarge
            name: control.symbol.length ? control.symbol : "check"
            visible: control.leads
            ink: control.selected ? Theme.containerText : Theme.text
        }
        SungText {
            id: label; x: control.leads ? 38 : control.removable ? 12 : (parent.width-implicitWidth)/2
            text: control.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium
            color: control.selected ? Theme.containerText : Theme.text; anchors.verticalCenter: parent.verticalCenter
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
        // An input chip stands for something entered, so it carries the means
        // to take it back out rather than needing somewhere else to undo it.
        AbstractButton {
            objectName: "chipRemove"
            visible: control.removable
            width: 26; height: parent.height
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            Accessible.name: "Remove " + control.text
            onClicked: control.removed()
            contentItem: Icon { name: "close"; size: Theme.chipIcon; ink: control.selected ? Theme.containerText : Theme.text }
        }
    }
}
