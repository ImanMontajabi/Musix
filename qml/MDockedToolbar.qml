import QtQuick
import QtQuick.Layouts

// Material 3 docked toolbar.
//
// Material retired the bottom app bar in favour of this: a strip of the actions
// that apply to what is on screen, docked to an edge rather than floating over
// it. It is 64dp tall on a container of its own, keeps 16dp clear at either
// end, and spaces what it holds between a floor and a ceiling rather than
// spreading it to the edges. A vibrant toolbar takes the primary container
// instead, for a strip that has to be noticed.
Rectangle {
    id: toolbar
    objectName: "dockedToolbar"

    property bool vibrant: false
    property alias spacing: row.spacing
    default property alias content: row.data

    implicitHeight: Theme.toolbarHeight
    // Material docks this strip to an edge, so it squares its corners.
    radius: 0
    color: vibrant ? Theme.primaryContainer : Theme.container

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.toolbarInset
        anchors.rightMargin: Theme.toolbarInset
        spacing: Theme.toolbarSpacingMin
    }
}
