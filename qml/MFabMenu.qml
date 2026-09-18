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
    readonly property real fabSize: 56

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
        anchors.right: fab.right
        anchors.bottom: fab.top
        anchors.bottomMargin: 12
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
                        font.pixelSize: Theme.labelLarge; emphasized: true
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
        width: root.fabSize; height: root.fabSize
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: root.open ? "Close actions" : (root.label || "Actions")
        onClicked: root.open = !root.open
        background: Rectangle {
            objectName: "fabShape"
            color: root.open ? Theme.high : Theme.primaryContainer
            // A FAB rests at the large shape step and morphs to full when it
            // becomes the menu's close button.
            radius: root.open ? Theme.shapeFull(fab.height) : Theme.shapeLargeIncreased
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
        contentItem: Icon {
            objectName: "fabIcon"
            anchors.centerIn: parent
            name: root.open ? "close" : root.symbol
            size: 24
            ink: root.open ? Theme.text : Theme.containerText
            rotation: root.open ? 90 : 0
            Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }
        }
    }
}
