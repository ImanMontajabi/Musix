import QtQuick

// Material 3 bottom sheet.
//
// Material puts a supporting pane in a bottom sheet once the window is too
// narrow to set one beside the content. The sheet sits on surfaceContainerLow,
// rounds only its top corners at the extra large step, carries a 32 by 4dp drag
// handle in onSurfaceVariant, and rests one level off the page. Dragging the
// handle down past a third of the sheet closes it.
Item {
    id: sheet

    property bool open: false
    property real peek: 0.6
    default property alias content: body.data
    signal closed()

    readonly property real restingY: sheet.open ? Math.max(0, parent.height-height) : parent.height
    property real drag: 0

    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    height: Math.round(parent ? parent.height*peek : 0)
    y: restingY + drag
    visible: y < (parent ? parent.height : 0)
    Behavior on y { enabled: app.motion && !grab.active; NumberAnimation { duration: Theme.springSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springSpatial } }
    onOpenChanged: drag = 0

    Rectangle {
        objectName: "bottomSheetSurface"
        anchors.fill: parent
        color: Theme.surface
        topLeftRadius: Theme.shapeExtraLarge
        topRightRadius: Theme.shapeExtraLarge
        MElevation { anchors.fill: parent; radius: Theme.shapeExtraLarge; level: 1 }

        // Material's own drag handle for a sheet: wider and flatter than the
        // one that splits panes, and centred on the leading edge.
        Item {
            id: handle
            objectName: "bottomSheetHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: 48; height: 22
            Rectangle {
                anchors.centerIn: parent
                width: 32; height: 4
                radius: Theme.shapeFull(height)
                color: Theme.muted
            }
            DragHandler {
                id: grab
                target: null; xAxis.enabled: false
                onActiveTranslationChanged: if (active) sheet.drag = Math.max(0, activeTranslation.y)
                onActiveChanged: if (!active) {
                    if (sheet.drag > sheet.height/3) { sheet.open = false; sheet.closed() }
                    sheet.drag = 0
                }
            }
        }
        Item {
            id: body
            anchors.fill: parent
            anchors.topMargin: handle.height
        }
    }
}
