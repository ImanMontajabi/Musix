import QtQuick
import QtQuick.Shapes

// Material 3 loading indicator.
//
// Material retired the indeterminate circular progress spinner in favour of a
// shape that morphs while it turns. The indeterminate form walks a sequence of
// seven shapes (soft burst, nine sided cookie, pentagon, pill, sunny, four
// sided cookie, oval), holding each for 650ms and adding a quarter turn per
// morph on top of a continuous rotation that takes 4666ms. The determinate form
// morphs a circle into the soft burst as progress runs from nought to one.
//
// Material builds the shapes from RoundedPolygon and morphs them by matching
// their cubic curves. Qt Quick has neither, so each shape here is written as
// the radius it carries at a given angle and a morph interpolates those radii.
// The silhouettes, the sequence and the timings are Material's; the curve
// matching underneath them is not.
Item {
    id: indicator

    property bool running: false
    property color ink: Theme.primary
    // Filling the container gives Material's contained variant; leaving it
    // clear gives the uncontained one, which is the default.
    property color trackColor: "transparent"
    property string label: "Loading"
    // Nought to one drives the circle-to-burst morph; below nought the
    // indicator runs its own indeterminate sequence instead.
    property real progress: -1
    readonly property bool determinate: progress >= 0

    readonly property bool animating: running && !determinate && visible && app.motion
        && Window.window !== null && Window.window.visible
        && Window.window.visibility !== Window.Minimized

    // Material's container is 48dp with a 38dp active indicator inside it.
    implicitWidth: 48
    implicitHeight: 48
    visible: running
    Accessible.role: Accessible.Indicator
    Accessible.name: label
    Accessible.ignored: !visible

    readonly property int shapeCount: 7
    readonly property int circleShape: 7
    readonly property int sampleCount: 96
    property int morphIndex: 0
    property real morphProgress: 0
    property real turn: 0
    property real spin: 0

    // A lobed shape: `count` bumps cut `depth` out of the radius, with
    // `point` deciding how sharply each one comes to a tip.
    function lobed(angle,count,depth,point) { return 1-depth*Math.pow((1-Math.cos(count*angle))/2,point) }
    // A regular polygon eased towards its circumcircle, which is what corner
    // rounding does to a silhouette.
    function polygonal(angle,sides,rounding) {
        const step=2*Math.PI/sides
        const edge=Math.cos(Math.PI/sides)/Math.cos(((angle%step)+step)%step-Math.PI/sides)
        return edge*(1-rounding)+rounding
    }
    function superellipse(angle,wide,tall,power) {
        return Math.pow(Math.pow(Math.abs(Math.cos(angle)/wide),power)+Math.pow(Math.abs(Math.sin(angle)/tall),power),-1/power)
    }
    function elliptical(angle,ratio,tilt) {
        const a=angle-tilt
        return ratio/Math.sqrt(Math.pow(ratio*Math.cos(a),2)+Math.pow(Math.sin(a),2))
    }
    function shapeRadius(shape,angle) {
        switch(shape) {
        case 0: return lobed(angle,10,0.15,1.1)   // soft burst
        case 1: return lobed(angle,9,0.20,1.0)    // nine sided cookie
        case 2: return polygonal(angle,5,0.30)    // pentagon
        case 3: return superellipse(angle,1,0.55,4.5) // pill
        case 4: return lobed(angle,8,0.20,0.6)    // sunny
        case 5: return lobed(angle,4,0.30,1.0)    // four sided cookie
        case 6: return elliptical(angle,0.64,-Math.PI/4) // oval
        }
        return 1
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.shapeFull(Math.min(width,height))
        color: indicator.trackColor
    }

    Shape {
        id: outline
        objectName: "loadingShape"
        anchors.centerIn: parent
        width: Math.min(indicator.width,indicator.height)*38/48
        height: width
        preferredRendererType: Shape.CurveRenderer
        // A morph turns the shape a further quarter as it runs, which is what
        // stops the sequence from ever settling into a loop the eye can follow.
        rotation: indicator.determinate ? 0 : indicator.morphProgress*90+indicator.turn+indicator.spin
        ShapePath {
            fillColor: indicator.ink
            strokeColor: "transparent"
            PathPolyline {
                path: {
                    const from=indicator.determinate ? indicator.circleShape : indicator.morphIndex
                    const to=indicator.determinate ? 0 : (indicator.morphIndex+1)%indicator.shapeCount
                    const held=indicator.determinate ? Math.min(1,indicator.progress) : indicator.morphProgress
                    const steps=indicator.sampleCount
                    const extent=outline.width/2
                    const points=[]
                    for(let i=0;i<=steps;++i) {
                        const angle=i*2*Math.PI/steps
                        const radius=(indicator.shapeRadius(from,angle)*(1-held)+indicator.shapeRadius(to,angle)*held)*extent
                        points.push(Qt.point(extent+radius*Math.cos(angle),extent+radius*Math.sin(angle)))
                    }
                    return points
                }
            }
        }
    }

    NumberAnimation {
        target: indicator; property: "spin"; from: 0; to: 360
        duration: 4666; loops: Animation.Infinite; running: indicator.animating
    }
    SequentialAnimation {
        running: indicator.animating; loops: Animation.Infinite
        NumberAnimation {
            target: indicator; property: "morphProgress"; from: 0; to: 1; duration: 450
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastSpatial
        }
        // Material holds the finished shape for the rest of its 650ms slot.
        PauseAnimation { duration: 200 }
        ScriptAction {
            script: {
                indicator.turn=(indicator.turn+90)%360
                indicator.morphIndex=(indicator.morphIndex+1)%indicator.shapeCount
                indicator.morphProgress=0
            }
        }
    }
}
