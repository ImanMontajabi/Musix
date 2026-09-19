import QtQuick
import QtQuick.Controls

// Material 3 menu item: a leading icon, the label, and trailing text for the
// keyboard shortcut that does the same thing. Material reserves the leading
// slot across the whole menu, so labels line up whether or not an individual
// item has an icon to put there.
MenuItem {
    id: control

    // The leading symbol. A checkable item shows its tick there instead.
    property string symbol: ""
    // The keyboard shortcut that reaches this item without the menu.
    property string shortcut: ""

    readonly property bool showsTick: checkable && checked
    readonly property bool hasLeading: showsTick || symbol.length > 0
    readonly property real leadingSpace: 32

    implicitHeight: 48
    height: visible ? implicitHeight : 0
    leftPadding: 14; rightPadding: 14
    palette.windowText: control.enabled ? Theme.text : Theme.muted
    indicator: Icon {
        objectName: "menuItemLeading"
        name: control.showsTick ? "check" : control.symbol
        size: 20; ink: control.enabled ? Theme.text : Theme.muted
        visible: control.hasLeading
        x: control.mirrored ? control.width-width-control.rightPadding : control.leftPadding
        // Set on the label's baseline rather than the row's centre line.
        y: (control.height-height)/2 + Math.round(Theme.labelLarge*0.115)
    }
    contentItem: Item {
        SungText {
            objectName: "menuItemLabel"
            anchors.verticalCenter: parent.verticalCenter
            x: control.mirrored ? 0 : control.leadingSpace
            width: parent.width-control.leadingSpace-(shortcutLabel.visible ? shortcutLabel.width+12 : 0)
            text: control.text; color: control.enabled ? Theme.text : Theme.muted
            opacity: control.enabled ? 1 : 0.5; font.pixelSize: Theme.bodyLarge
            elide: Text.ElideRight
        }
        SungText {
            id: shortcutLabel
            objectName: "menuItemShortcut"
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            visible: control.shortcut.length > 0
            text: control.shortcut
            color: Theme.muted
            font.pixelSize: Theme.labelMedium
            Accessible.ignored: true
        }
    }
    Accessible.name: control.text + (control.shortcut ? ", " + control.shortcut : "")
    background: Item {
        Rectangle {
            anchors.fill: parent; radius: Theme.shapeMedium; color: Theme.text
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.highlighted ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeSmall; color: "transparent"; border.color: Theme.primary; border.width: 2; visible: control.visualFocus }
    }
}
