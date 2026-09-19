import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property string symbol: ""
    // Secondary destinations are collections, so they identify themselves by cover.
    property string artUrl: ""
    property bool selected: false
    property bool expanded: false
    // Pending work in this destination. A count draws Material's large badge,
    // `badged` alone draws the dot.
    property bool badged: false
    property int badgeCount: -1
    implicitWidth: expanded ? 220 : 80
    // Material's rail item container is 64dp tall.
    implicitHeight: expanded ? 56 : 64
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected
    background: Item {
        Rectangle {
            id: pill
            objectName: "navigationIndicator"
            // M3 lets the expanded indicator fill its container rather than hug
            // the label; the target area spans the full rail either way.
            x: control.expanded ? 0 : (parent.width-width)/2
            y: control.expanded ? 0 : 0
            // Material's active indicator is 56 by 32 where the destination is
            // stacked, and fills its container where it is laid out in a row.
            width: control.expanded ? parent.width : 56
            height: control.expanded ? parent.height : 32
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
            color: "transparent"; border.color: Theme.focusRing; border.width: 2
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        Item {
            objectName: "navigationStack"
            anchors.fill: parent
            visible: opacity>0; opacity: control.expanded ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            Icon { id: stackGlyph; anchors.horizontalCenter: parent.horizontalCenter; y: 8; name: control.symbol; fill: control.selected ? 1 : 0; ink: control.selected ? Theme.primary : Theme.text; Accessible.ignored: true }
            MBadge {
                objectName: "navigationBadge"
                present: control.badged || control.badgeCount >= 0
                count: control.badgeCount; subject: control.text
                x: stackGlyph.x+stackGlyph.width-inset; y: stackGlyph.y-height+lift
            }
            SungText {
                objectName: "navigationLabel"
                y: 38; width: parent.width; height: 20
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
                Icon { anchors.centerIn: parent; visible: !control.artUrl; name: control.symbol; fill: control.selected ? 1 : 0; ink: control.selected ? Theme.primary : Theme.text; Accessible.ignored: true }
                Artwork { anchors.centerIn: parent; visible: !!control.artUrl; width: 24; height: 24; radius: Theme.shapeSmall; pixels: 96; url: control.artUrl }
                MBadge {
                    objectName: "navigationWideBadge"
                    present: control.badged || control.badgeCount >= 0
                    count: control.badgeCount; subject: control.text
                    x: parent.width-inset; y: -height+lift
                }
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
