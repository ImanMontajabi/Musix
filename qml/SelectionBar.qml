import QtQuick
import QtQuick.Layouts

// What to do with the songs that are selected.
//
// Material calls this a docked toolbar: the actions that apply to what is on
// screen, on a container of their own and docked across the surface rather
// than floating loose above the list.
MDockedToolbar {
    id: bar
    required property var view
    property bool canRemove: false
    visible: view.selection.count>0
    objectName: "selectionToolbar"

    MButton { symbol: "close"; tip: "Clear selection · Esc"; implicitWidth: 40; implicitHeight: 40; onClicked: bar.view.selection.clear() }
    SungText { text: bar.view.selection.count+" selected"; Layout.fillWidth: true; font.pixelSize: Theme.labelLarge }
    MButton { symbol: "next"; tip: "Play selected next"; implicitWidth: 40; implicitHeight: 40; onClicked: {const before=app.queue.count;app.enqueueItems(bar.view.selection.items(),true);if(app.queue.count>before)confirm();} }
    MButton { symbol: "queue"; tip: "Add selected to queue"; implicitWidth: 40; implicitHeight: 40; onClicked: {const before=app.queue.count;app.enqueueItems(bar.view.selection.items());if(app.queue.count>before)confirm();} }
    MButton { objectName: bar.view.queueMode?"bulkQueuePlaylist":"bulkCollectionPlaylist"; symbol: "plus"; tip: "Add selected to playlist"; implicitWidth: 40; implicitHeight: 40; onClicked: bar.view.addSelected() }
    MButton { symbol: "remove"; tip: "Remove selected · Delete"; visible: bar.canRemove; implicitWidth: 40; implicitHeight: 40; onClicked: bar.view.removeSelected() }
}
