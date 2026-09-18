import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property bool selected: false
    property bool selectable: true
    implicitWidth: label.implicitWidth + 24 + (selectable ? 26 : 0)
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: selectable ? Accessible.CheckBox : Accessible.Button
    Accessible.checkable: selectable
    Accessible.checked: selected
    readonly property bool dimmed: !enabled
    background: Rectangle {
        y: 4; height: control.height - 8; radius: Theme.shapeSmall
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
            color: "transparent"; border.width: 2; border.color: Theme.primary
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        opacity: control.dimmed ? Theme.disabledContentOpacity : 1
        Icon { x: 12; name: "check"; size: 18; visible: control.selectable && control.selected; ink: Theme.containerText; anchors.verticalCenter: parent.verticalCenter }
        SungText {
            id: label; x: control.selectable && control.selected ? 38 : (parent.width-implicitWidth)/2
            text: control.text; font.pixelSize: Theme.labelLarge; font.weight: Font.Medium
            color: control.selected ? Theme.containerText : Theme.text; anchors.verticalCenter: parent.verticalCenter
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        }
    }
}
