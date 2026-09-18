import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property string symbol: ""
    // Secondary destinations are collections, so they identify themselves by cover.
    property string artUrl: ""
    property bool selected: false
    property bool expanded: false
    implicitWidth: expanded ? 220 : 80
    implicitHeight: expanded ? 56 : 68
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected
    background: Item {
        Rectangle {
            id: pill
            // M3 lets the expanded indicator fill its container rather than hug
            // the label; the target area spans the full rail either way.
            x: control.expanded ? 0 : (parent.width-width)/2
            y: control.expanded ? 0 : 0
            width: control.expanded ? parent.width : 64
            height: control.expanded ? parent.height : 40
            radius: control.down ? 14 : 20
            color: control.selected ? Theme.high : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            Behavior on radius { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.8 } }
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: Theme.primary
                opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
            }
        }
        Rectangle {
            objectName: "navigationFocusRing"
            anchors.fill: parent; anchors.margins: 2; radius: Theme.shapeLarge
            color: "transparent"; border.color: Theme.primary; border.width: 2
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Item {
            objectName: "navigationStack"
            anchors.fill: parent
            visible: opacity>0; opacity: control.expanded ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            Icon { anchors.horizontalCenter: parent.horizontalCenter; y: 8; name: control.symbol; ink: control.selected ? Theme.primary : Theme.text; Accessible.ignored: true }
            SungText {
                objectName: "navigationLabel"
                y: 42; width: parent.width; height: 20
                text: control.text; horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.labelMedium
                font.weight: control.selected ? Font.DemiBold : Font.Medium
                color: control.selected ? Theme.primary : Theme.muted
                Accessible.ignored: true
            }
        }
        Row {
            objectName: "navigationRow"
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12
            spacing: 12
            visible: opacity>0; opacity: control.expanded ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            Item {
                width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; visible: !control.artUrl; name: control.symbol; ink: control.selected ? Theme.primary : Theme.text; Accessible.ignored: true }
                Artwork { anchors.centerIn: parent; visible: !!control.artUrl; width: 24; height: 24; radius: Theme.shapeSmall; pixels: 96; url: control.artUrl }
            }
            SungText {
                objectName: "navigationWideLabel"
                width: parent.width-36; height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: control.text; elide: Text.ElideRight
                font.pixelSize: Theme.labelLarge
                font.weight: control.selected ? Font.DemiBold : Font.Medium
                color: control.selected ? Theme.primary : Theme.text
                Accessible.ignored: true
            }
        }
    }
}
