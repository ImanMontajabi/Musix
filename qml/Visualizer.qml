import QtQuick
import Sung.Native 1.0

// What moves behind the full-screen player. It reads the analysed levels, so it
// follows the song on macOS too, and it runs on the GPU: a shader driven by the
// display's own frame clock, never a Canvas repainted on the CPU.
//
// It only runs while it can be seen and the song is playing. Otherwise, and
// always with Animations or Reduce motion off, it holds one still frame.
Item {
    id: vis
    objectName: "visualizer"
    property string kind: app.visualizer
    readonly property bool shown: kind !== "off"
    readonly property bool running: !!(shown && visible && Theme.motion && app.playing && app.uiActive
                                       && Window.window && Window.window.visible && Window.window.visibility !== Window.Minimized)
    visible: shown

    readonly property var bands: running ? app.audioLevels : [0, 0, 0, 0, 0]
    readonly property real overall: ((Number(bands[0]) || 0) + (Number(bands[1]) || 0) + (Number(bands[2]) || 0)
                                     + (Number(bands[3]) || 0) + (Number(bands[4]) || 0)) / 5
    property real level: overall
    Behavior on level { enabled: vis.running; NumberAnimation { duration: 300 } }
    // A hit is the bass standing above its own recent average, the same test
    // the play button breathes by.
    readonly property real bass: Number(bands[0]) || 0
    property real bassFast: bass
    property real bassSlow: bass
    Behavior on bassFast { enabled: vis.running; NumberAnimation { duration: 80 } }
    Behavior on bassSlow { enabled: vis.running; NumberAnimation { duration: 900 } }

    // Integrated rather than computed from a clock, so a change of speed
    // changes how fast the rain falls, not where it is.
    property real flow: 0
    property real burst: 0
    property real clock: 0
    property real lastBurst: -10
    // Nothing may flash more than three times a second: a burst waits at least
    // this long after the last one, and fades rather than cutting off.
    readonly property real burstSpacing: 0.34
    function step(dt) {
        clock += dt
        flow += dt * (2.5 + 9 * level)
        burst *= Math.exp(-dt / 0.22)
        const hit = bassFast - bassSlow
        if (hit > 0.06 && clock - lastBurst >= burstSpacing) {
            burst = Math.min(1, hit * 5)
            lastBurst = clock
        }
    }
    FrameAnimation {
        running: vis.running
        onTriggered: vis.step(Math.min(frameTime, 0.1))
    }

    // One colour from the cover itself, whatever the accent is set to.
    RoundedArt {
        id: sampler
        visible: false; pixels: 48
        source: vis.shown ? (app.current.art || "") : ""
        onReadyChanged: vis.coverSeed = ready ? seedColor() : "transparent"
    }
    property color coverSeed: "transparent"
    // Trails in container tones, which sit well away from the text in either
    // theme; only the leading pixel takes the accent itself.
    readonly property color trailColor: Theme.blend(Theme.primaryContainer, Theme.primary, 0.3)
    readonly property color coverColor: coverSeed.a > 0 ? Theme.blend(coverSeed, Theme.background, 0.45) : Theme.tertiaryContainer
    readonly property color headColor: Theme.primary

    Loader {
        anchors.fill: parent
        active: vis.kind === "pixelRain"
        sourceComponent: ShaderEffect {
            objectName: "pixelRain"
            property real flow: vis.flow
            // A still frame shows a moderate rain rather than an empty one.
            property real level: vis.running ? vis.level : 0.45
            property real burst: vis.running ? vis.burst : 0
            property real cell: 10
            property size size: Qt.size(width, height)
            property color body: vis.trailColor
            property color cover: vis.coverColor
            property color head: vis.headColor
            fragmentShader: "qrc:/shaders/pixelrain.frag.qsb"
        }
    }
    // Text and controls sit on this, not on the rain.
    Rectangle { anchors.fill: parent; color: Theme.background; opacity: 0.2 }
}
