import QtQuick
import QtQuick.Controls

// Material 3 FAB and FAB menu.
//
// The FAB carries the primary action of a surface. Where that action has
// several related forms, the specification says to open a FAB menu of two to
// six of them, and that this replaces stacked small FABs and speed dials.
//
// Opening morphs the FAB into the menu's close button, which is the shape
// morph Material asks for on a change of state, and the items arrive one after
// another on the spatial spring rather than all at once.
Item {
    id: root
    objectName: "fabMenu"

    // Each action is {label, symbol, action}.
    property var actions: []
    property string symbol: "plus"
    property string label: ""
    property bool open: false
    readonly property int count: actions.length
    // Material's small FAB, which is the size it gives a secondary action:
    // 40dp at the medium corner, still carrying a 24dp glyph. Adding to the
    // library is not the screen's primary action, and at 56dp the FAB outweighed
    // everything around it.
    readonly property real fabSize: 40
    // Material's rail carries the FAB or extended FAB at its head, above the
    // destinations. A FAB up there opens downward, and towards the content
    // rather than away from it.
    property bool downward: false
    property bool leadingEdge: false
    // An extended FAB says in words what the action is. Material's small
    // extended FAB is 56dp with a 16dp corner, the label at title medium and
    // 16dp of padding either side of it.
    property bool extended: false

    implicitWidth: fab.width
    implicitHeight: fab.height
    z: 40

    function close() { open = false }
    onOpenChanged: open ? menu.open() : menu.close()

    // The menu is a temporary surface over the content, which is what Material
    // calls it, so it is a Popup: that puts it in the window's overlay instead
    // of inside whatever holds the FAB. In the rail the FAB sits in a 40dp
    // slot beside the destinations, and a menu drawn there would be painted
    // under the content surface and cut off at the rail's edge.
    Popup {
        id: menu
        objectName: "fabMenuPopup"
        parent: fab
        padding: 0
        background: null
        // Blocks what is behind it and dismisses on a press there, without
        // darkening it: the FAB menu shades nothing in the specification.
        modal: true
        dim: false
        // A press anywhere else closes it, the FAB included: the menu blocks
        // what is behind it, so that press never reaches the button, and
        // pressing the button a second time reads as closing the menu.
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.open = false
        // The menu opens from the FAB's own edge, towards the content.
        x: root.leadingEdge ? 0 : fab.width - width
        y: root.downward ? fab.height + 12 : -height - 12
        // What keeps the actions reachable: a menu wider than the room on that
        // side is moved back inside the window rather than hanging off it.
        margins: 12
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: app.motion ? Theme.springFastEffectsMs : 0 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: app.motion ? Theme.springFastEffectsMs : 0 } }

        contentItem: Column {
            id: items
            objectName: "fabMenuItems"
            spacing: 8
            // Items arrive from the FAB, nearest first. The column owns their
            // y, so the arrival is its own add transition: an item that
            // animates its own y fights the column, and every item lands on
            // the first. The menu fades in as a whole above, so an item's
            // opacity is left alone and cannot be stranded part way.
            add: Transition {
                enabled: app.motion
                SequentialAnimation {
                    PauseAnimation { duration: Math.min(3, ViewTransition.index) * 40 }
                    NumberAnimation {
                        property: "y"; from: ViewTransition.destination.y + (root.downward ? -16 : 16)
                        duration: Theme.springFastSpatialMs
                        easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial
                    }
                }
            }
            Repeater {
                model: root.open ? root.actions : []
                delegate: AbstractButton {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "fabMenuItem_" + index
                    // The column places every item at its own leading edge, so
                    // the row that is not the widest is aligned by hand,
                    // towards the edge the menu opened from.
                    x: root.leadingEdge ? 0 : parent.width - width
                    height: 56
                    implicitWidth: entryLabel.implicitWidth + 72
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: modelData.label
                    onClicked: { root.close(); if (modelData.action) modelData.action() }
                    background: Rectangle {
                        radius: Theme.shapeFull(entry.height)
                        color: Theme.primaryContainer
                        Rectangle {
                            anchors.fill: parent; radius: parent.radius
                            color: Theme.containerText
                            opacity: entry.down || entry.visualFocus ? Theme.pressedOpacity : entry.hovered ? Theme.hoverOpacity : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs } }
                        }
                    }
                    contentItem: Row {
                        anchors.centerIn: parent
                        spacing: 12
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: entry.modelData.symbol || ""; size: 24; ink: Theme.containerText }
                        SungText {
                            id: entryLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.label; color: Theme.containerText
                            // Material's menu items carry a plain label-large, not
                            // an emphasized one; the icon beside it is the weight.
                            font.pixelSize: Theme.labelLarge; labelRole: true
                        }
                    }
                }
            }
        }
    }

    AbstractButton {
        id: fab
        objectName: "fab"
        width: root.extended && !root.open
                 ? Theme.extendedFabInset*2 + 24 + Theme.extendedFabGap + fabLabel.implicitWidth
                 : root.extended ? Theme.extendedFabHeight : root.fabSize
        height: root.extended ? Theme.extendedFabHeight : root.fabSize
        Behavior on width { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: root.open ? "Close actions" : (root.label || "Actions")
        onClicked: root.open = !root.open
        background: Rectangle {
            objectName: "fabShape"
            color: root.open ? Theme.high : Theme.primaryContainer
            // A small FAB rests at the medium shape step and morphs to full
            // when it becomes the menu's close button.
            radius: root.open ? Theme.shapeFull(fab.height) : root.extended ? Theme.shapeLarge : Theme.shapeMedium
            Behavior on radius { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
            MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: Theme.containerText
                opacity: fab.down || fab.visualFocus ? Theme.pressedOpacity : fab.hovered ? Theme.hoverOpacity : 0
                Behavior on opacity { NumberAnimation { duration: Theme.springFastEffectsMs } }
            }
        }
        // A control stretches its content item to fill it, so the glyph needs a
        // wrapper to keep the 24dp Material asks for inside a 56dp FAB.
        contentItem: Item {
            SungText {
                id: fabLabel
                objectName: "fabLabel"
                visible: root.extended && !root.open
                text: root.label
                font.pixelSize: Theme.titleMedium
                labelRole: true
                color: Theme.containerText
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.extendedFabInset + 24 + Theme.extendedFabGap
            }
            Icon {
                objectName: "fabIcon"
                anchors.verticalCenter: parent.verticalCenter
                x: fabLabel.visible ? Theme.extendedFabInset : (parent.width-width)/2
                name: root.open ? "close" : root.symbol
                size: 24
                ink: root.open ? Theme.text : Theme.containerText
                rotation: root.open ? 90 : 0
                Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
            }
        }
    }
}
