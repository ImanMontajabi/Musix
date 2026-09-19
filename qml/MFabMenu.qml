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

    // The scrim sits behind the menu so a click outside dismisses it, which is
    // how the menu stays modal without becoming a dialog.
    MouseArea {
        objectName: "fabMenuScrim"
        parent: root.parent
        anchors.fill: parent
        visible: root.open
        onClicked: root.close()
    }

    Column {
        id: items
        objectName: "fabMenuItems"
        anchors.right: root.leadingEdge ? undefined : fab.right
        anchors.left: root.leadingEdge ? fab.left : undefined
        anchors.bottom: root.downward ? undefined : fab.top
        anchors.top: root.downward ? fab.bottom : undefined
        anchors.bottomMargin: 12
        anchors.topMargin: 12
        spacing: 8
        visible: root.open || fadeOut.running
        Repeater {
            model: root.open ? root.actions : []
            delegate: AbstractButton {
                id: entry
                required property var modelData
                required property int index
                objectName: "fabMenuItem_" + index
                anchors.right: parent.right
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
                // Items arrive from the FAB, nearest first.
                opacity: 0
                y: 16
                Component.onCompleted: arrive.start()
                ParallelAnimation {
                    id: arrive
                    NumberAnimation { target: entry; property: "opacity"; to: 1; duration: Theme.springFastEffectsMs }
                    NumberAnimation {
                        target: entry; property: "y"; to: 0
                        duration: Theme.springFastSpatialMs
                        easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial
                    }
                }
            }
        }
        NumberAnimation { id: fadeOut; target: items; property: "opacity"; to: 1; duration: 0 }
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
