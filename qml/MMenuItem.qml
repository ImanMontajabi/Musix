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
    // Material's segmented menu draws its items as one run: each one carries a
    // container of its own, nearly square inside the run and round at its
    // ends, set apart rather than divided by a rule. The menu sets these.
    property bool segmented: false
    property bool firstInRun: false
    property bool lastInRun: false
    // A vibrant menu takes the tertiary container, for a menu opened over
    // something a surface would disappear into.
    property bool vibrant: false
    // Material's menu marks a chosen item with the tertiary container, not the
    // secondary one that marks a chosen anything else, and a vibrant menu,
    // already tertiary, deepens to the tertiary role itself.
    readonly property color ink: !control.enabled ? Theme.muted
                               : control.vibrant ? (control.checked ? Theme.tertiaryText : Theme.tertiaryContainerText)
                               : control.checked ? Theme.tertiaryContainerText : Theme.text
    // The leading icon is the variant ink until the item is chosen, when it
    // takes the ink of the container it has been given.
    readonly property color leadingInk: !control.enabled ? Theme.muted
                                      : control.vibrant || control.checked ? ink : Theme.muted

    readonly property bool showsTick: checkable && checked
    readonly property bool hasLeading: showsTick || symbol.length > 0
    readonly property real leadingSpace: 32

    implicitHeight: segmented ? 44 : 48
    height: visible ? implicitHeight : 0
    leftPadding: 14; rightPadding: 14
    palette.windowText: control.ink
    indicator: Icon {
        objectName: "menuItemLeading"
        name: control.showsTick ? "check" : control.symbol
        size: 20; ink: control.leadingInk
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
            text: control.text; color: control.ink
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
        // The run's own container. Material rounds the ends of the run and
        // leaves the corners inside it nearly square, and rounds an item fully
        // while it is taken.
        Rectangle {
            objectName: "menuItemContainer"
            visible: control.segmented
            y: 1; height: parent.height-2
            width: parent.width
            readonly property bool taken: control.down || control.visualFocus || control.highlighted || control.checked
            // The item under the pointer takes the shape Material publishes
            // for it, and so do the ends of the run; the corners inside the
            // run are the small step.
            topLeftRadius: taken || control.firstInRun ? Theme.menuItemTaken : Theme.shapeSmall
            topRightRadius: topLeftRadius
            bottomLeftRadius: taken || control.lastInRun ? Theme.menuItemTaken : Theme.shapeSmall
            bottomRightRadius: bottomLeftRadius
            color: control.checked ? (control.vibrant ? Theme.tertiary : Theme.tertiaryContainer)
                                   : (control.vibrant ? Theme.tertiaryContainer : Theme.surfaceLow)
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle {
            anchors.fill: parent; radius: Theme.shapeMedium; color: control.ink
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.highlighted ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeSmall; color: "transparent"; border.color: Theme.focusRing; border.width: 2; visible: control.visualFocus }
    }
}
