import QtQuick

// Material 3 navigation motion.
//
// Material separates two cases. Destinations reached from the rail have no
// spatial relationship to each other, so they *fade through*: the outgoing view
// leaves over the first 30% of the transition, and the incoming one arrives
// over the remaining 70% while growing from 92%. Tabs inside a destination are
// peers laid out along one line, so they share the *X axis*: the outgoing view
// leaves towards the side it came from and the incoming one arrives from the
// other, travelling 30dp.
//
// Opening a detail out of a list is neither of those: the two screens sit at
// consecutive levels of one hierarchy, so Material slides them horizontally
// while fading, and the direction says which way you went.
//
// The content only swaps once the outgoing half has finished, which is why the
// caller hands over the navigation itself rather than performing it first.
QtObject {
    id: root

    // The item to move. It needs a `shift` property fed into a Translate, so
    // the X axis can be travelled without fighting the layout that placed it.
    property Item target: null

    // M3 gives the whole transition 300ms and the outgoing half 30% of it.
    readonly property int totalDuration: 300
    readonly property int leaveDuration: Math.round(totalDuration*0.3)
    readonly property int arriveDuration: totalDuration-leaveDuration
    readonly property real arriveScale: 0.92
    readonly property real axisTravel: 30

    readonly property bool running: leave.running || arrive.running
    property var pending: null
    property real travel: 0

    signal navigated()

    // Destinations with no spatial relationship.
    function fadeThrough(action) { begin(action,0) }
    // Peers along a line. `forward` is the direction of travel through them.
    function sharedAxisX(action,forward) { begin(action,forward?axisTravel:-axisTravel) }
    // Consecutive levels of a hierarchy: opening a detail, and coming back out
    // of it. Material moves these horizontally with a fade rather than fading
    // them through each other, because the two screens are related.
    function forwardBackward(action,forward) { begin(action,forward?axisTravel:-axisTravel) }

    function begin(action,distance) {
        if(!target || !app.motion) { settle(); if(action)action(); navigated(); return }
        if(running)settle()
        travel=distance
        pending=action
        leave.start()
    }

    // Hand the target back exactly as it was found, whatever was in flight.
    function settle() {
        leave.stop();arrive.stop()
        if(target){target.opacity=1;target.scale=1;target.shift=0}
    }

    function swap() {
        const action=pending
        pending=null
        if(action)action()
        navigated()
    }

    property Animation leave: ParallelAnimation {
        NumberAnimation {
            target: root.target; property: "opacity"; to: 0; duration: root.leaveDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.exitCurve
        }
        NumberAnimation {
            target: root.target; property: "shift"; to: -root.travel; duration: root.leaveDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.exitCurve
        }
        onFinished: {
            root.swap()
            if(root.target){
                // Fading through shrinks the arriving view; sharing an axis
                // slides it in at full size from the far side.
                root.target.scale=root.travel===0?root.arriveScale:1
                root.target.shift=root.travel===0?0:root.travel
            }
            root.arrive.start()
        }
    }

    property Animation arrive: ParallelAnimation {
        NumberAnimation {
            target: root.target; property: "opacity"; to: 1; duration: root.arriveDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.enterCurve
        }
        NumberAnimation {
            target: root.target; property: "scale"; to: 1; duration: root.arriveDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.enterCurve
        }
        NumberAnimation {
            target: root.target; property: "shift"; to: 0; duration: root.arriveDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.enterCurve
        }
    }
}
