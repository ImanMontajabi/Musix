import QtQuick
import QtQuick.Controls

// Material 3 split button.
//
// "Split buttons are made of a common button and a menu icon button." The two
// sit together as one shape, so the corners that face each other are small
// while the outer corners stay full. Material calls these inner corners, and
// they are the reason a split button reads as one control rather than two.
//
// Activating the menu spins its chevron and morphs the button towards a square,
// which is the interaction-state shape morph the specification asks for.
Item {
    id: control
    objectName: "splitButton"

    property string text: ""
    property string symbol: ""
    property bool filled: true
    property bool tonal: false
    property alias menu: menuLoader.sourceComponent
    property bool menuOpen: false
    signal clicked()

    // Material's small split button: a 40dp container inside the 48dp target
    // it keeps, 2dp between the halves, 16dp before the label and 12dp after
    // it, and 13dp either side of a 22dp chevron.
    readonly property real unit: 40
    readonly property real target: Math.max(Theme.minimumTarget, unit)
    readonly property real leadingSpace: 16
    readonly property real trailingSpace: 12
    readonly property real chevronSpace: 13
    readonly property real chevronSize: 22
    readonly property real outer: Theme.shapeFull(unit)
    // The facing corners sit at the bottom of the scale, so the seam reads as
    // one control divided rather than two controls placed side by side.
    readonly property real inner: Theme.shapeExtraSmall
    implicitWidth: pair.implicitWidth
    implicitHeight: target

    Row {
        id: pair
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        height: control.unit
        spacing: 2

    AbstractButton {
        id: action
        objectName: "splitButtonAction"
        height: control.unit
        implicitWidth: label.implicitWidth + (control.symbol.length ? 32 : 0)
                       + control.leadingSpace + control.trailingSpace
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        text: control.text
        readonly property color ink: control.filled ? Theme.primaryText : Theme.primary
        Accessible.name: control.text
        onClicked: control.clicked()
        background: Rectangle {
            color: control.filled ? Theme.primary : control.tonal ? Theme.high : "transparent"
            topLeftRadius: control.outer; bottomLeftRadius: control.outer
            topRightRadius: control.inner; bottomRightRadius: control.inner
            border.width: control.filled || control.tonal ? 0 : 1
            border.color: Theme.controlOutline
            Rectangle {
                anchors.fill: parent
                topLeftRadius: parent.topLeftRadius; bottomLeftRadius: parent.bottomLeftRadius
                topRightRadius: parent.topRightRadius; bottomRightRadius: parent.bottomRightRadius
                color: control.filled ? Theme.primaryText : Theme.primary
                opacity: action.down || action.visualFocus ? Theme.pressedOpacity : action.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            }
        }
        // Laid out the way every other button in the app lays out, so a split
        // button reads as one of them rather than as a lookalike.
        contentItem: Item {
            Row {
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                // The leading half is full cornered on the outside and small on
                // the inside, so Material nudges its content towards the flat
                // edge to make it look centred.
                x: (parent.width-width)/2 + Theme.opticalShift(control.outer, control.inner)
                Item {
                    width: 24; height: 24
                    anchors.verticalCenter: parent.verticalCenter
                    visible: control.symbol.length > 0
                    Icon { anchors.centerIn: parent; name: control.symbol; ink: action.ink }
                }
                SungText {
                    id: label
                    anchors.verticalCenter: parent.verticalCenter
                    text: control.text
                    font.pixelSize: Theme.labelLarge
                    emphasized: true; labelRole: true
                    color: action.ink
                }
            }
        }
    }

    AbstractButton {
        id: reveal
        objectName: "splitButtonMenu"
        width: control.chevronSpace*2 + control.chevronSize; height: control.unit
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: "More options for " + control.text
        onClicked: {
            menuLoader.active = true
            if (!menuLoader.item) return
            control.menuOpen = true
            menuLoader.item.popup(reveal, reveal.width-menuLoader.item.width, reveal.height+4)
        }
        background: Rectangle {
            objectName: "splitButtonMenuShape"
            color: control.filled ? Theme.primary : control.tonal ? Theme.high : "transparent"
            topLeftRadius: control.inner; bottomLeftRadius: control.inner
            // Opening morphs the outer corners towards a square, so the control
            // visibly changes state rather than only showing a menu.
            topRightRadius: control.menuOpen ? Theme.shapeMedium : control.outer
            bottomRightRadius: control.menuOpen ? Theme.shapeMedium : control.outer
            border.width: control.filled || control.tonal ? 0 : 1
            border.color: Theme.controlOutline
            Behavior on topRightRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Behavior on bottomRightRadius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Rectangle {
                anchors.fill: parent
                topLeftRadius: parent.topLeftRadius; bottomLeftRadius: parent.bottomLeftRadius
                topRightRadius: parent.topRightRadius; bottomRightRadius: parent.bottomRightRadius
                color: control.filled ? Theme.primaryText : Theme.primary
                opacity: reveal.down || reveal.visualFocus ? Theme.pressedOpacity : reveal.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            }
        }
        contentItem: Item {
            Icon {
                objectName: "splitButtonChevron"
                name: "chevron"
                // Small corner leading, full corner trailing: the nudge goes
                // the other way from the action half.
                x: (parent.width-width)/2 + Theme.opticalShift(control.inner, control.outer)
                y: (parent.height-height)/2
                ink: action.ink
                // The chevron points the way the menu opens, and spins to point
                // back at the button once it is open.
                rotation: control.menuOpen ? 270 : 90
                Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            }
        }
    }

    }

    // The menu is built the first time it is asked for. Instantiating a popup
    // for every collection page that is never opened costs memory and puts an
    // extra focus target in the scene for nothing.
    Loader {
        id: menuLoader
        active: false
        onItemChanged: if (item) item.closed.connect(function(){ control.menuOpen = false })
    }
}
