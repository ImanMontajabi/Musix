import QtQuick
import QtQuick.Shapes

// Material 3 pull to refresh, drawn with the Expressive loading indicator.
//
// The loading indicator replaced the indeterminate circular progress spinner,
// and pull-to-refresh is the interaction Material names for it. It uses shape
// and motion to hold attention: here the shape morphs from a soft square into a
// circle as the list is pulled, then spins while the refresh runs.
Item {
    id: refresher
    objectName: "pullToRefresh"

    // The list being pulled, and what to do once it has been pulled far enough.
    property Flickable target: null
    property bool busy: false
    property bool enabled: true
    signal triggered()

    readonly property real threshold: 72
    // How far past the top the list has been dragged.
    readonly property real pulled: target && enabled ? Math.max(0, -(target.contentY - target.originY) - target.topMargin) : 0
    readonly property real progress: Math.max(0, Math.min(1, pulled/threshold))
    readonly property bool armed: progress >= 1

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    y: busy ? 16 : Math.min(refresher.threshold, refresher.pulled) - height
    width: 40; height: 40
    visible: enabled && (pulled > 0 || busy)
    opacity: busy ? 1 : progress
    z: 20
    Behavior on y { enabled: app.motion && refresher.busy; NumberAnimation { duration: Theme.springFastSpatialMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial } }

    // Releasing past the threshold refreshes; releasing short of it does not.
    Connections {
        target: refresher.target
        enabled: refresher.enabled
        function onDraggingChanged() {
            if (!refresher.target.dragging && refresher.armed && !refresher.busy)
                refresher.triggered()
        }
    }

    Rectangle {
        id: puck
        anchors.fill: parent
        color: Theme.primaryContainer
        // Shape morph: a soft square becomes a circle as the pull completes.
        radius: Theme.shapeSmall + (Theme.shapeFull(width)-Theme.shapeSmall)*refresher.progress
        rotation: refresher.busy ? 0 : refresher.progress*90
        Behavior on rotation { enabled: app.motion; NumberAnimation { duration: Theme.springFastEffectsMs } }

        Shape {
            anchors.centerIn: parent
            width: 22; height: 22
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.containerText
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: 11; centerY: 11; radiusX: 9; radiusY: 9
                    startAngle: -90
                    sweepAngle: refresher.busy ? 280 : 300*refresher.progress
                }
            }
            RotationAnimation on rotation {
                id: spin
                running: refresher.busy && app.motion
                loops: Animation.Infinite
                from: 0; to: 360; duration: 900
            }
        }
    }
}
