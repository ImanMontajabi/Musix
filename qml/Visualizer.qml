import QtQuick
import Sung.Native 1.0

// What moves behind the full-screen player. It reads the analysed spectrum and
// beats at the position being heard, so it follows the song on macOS too, and
// it runs on the GPU: a shader driven by the display's own frame clock, never
// a Canvas repainted on the CPU.
//
// It only runs while it can be seen and the song is playing. Otherwise, and
// always with Animations or Reduce motion off, it holds one still frame.
Item {
    id: vis
    objectName: "visualizer"
    property string kind: app.visualizer
    // What the rain keeps out from behind: the cover, the text, the controls.
    property list<Item> guards
    readonly property bool shown: kind !== "off"
    readonly property bool running: !!(shown && visible && Theme.motion && app.playing && app.uiActive
                                       && Window.window && Window.window.visible && Window.window.visibility !== Window.Minimized)
    visible: shown

    // Fast attack, slow release: a band rises the frame the music does and
    // falls away over about a third of a second, so hits read as hits.
    readonly property real release: 0.35
    property var bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property real level: 0
    // Integrated rather than computed from a clock, so a change of speed
    // changes how fast the rain falls, not where it is.
    property real flow: 0
    property real clock: 0
    // A beat sends a wave down through the rain. Nothing may flash more than
    // three times a second: the analysis already spaces beats that far apart,
    // and a wave that would come sooner is dropped here as well.
    readonly property real beatSpacing: 0.34
    property real lastWave: -10
    property real waveAge: 10
    property real waveStrength: 0
    function hit(strength) {
        if (clock - lastWave < beatSpacing)
            return false
        lastWave = clock
        waveAge = 0
        waveStrength = Math.min(1, Math.max(0.3, strength))
        // Snake's food goes where the loudest band is drawn.
        let loudest = 0
        for (let i = 1; i < 16; ++i)
            if ((bands[i] || 0) > (bands[loudest] || 0))
                loudest = i
        if (snakeLoader.item)
            snakeLoader.item.beat(waveStrength, (loudest + 0.5) / 16)
        return true
    }
    function step(dt) {
        clock += dt
        waveAge += dt
        const target = app.spectrum
        const decay = Math.exp(-dt / release)
        const next = []
        let sum = 0
        for (let i = 0; i < 16; ++i) {
            const v = Math.max(Number(target[i]) || 0, (bands[i] || 0) * decay)
            next.push(v)
            sum += v
        }
        bands = next
        level = sum / 16
        // A short surge right after a beat.
        const surge = waveAge < 0.3 ? 1 + 1.5 * waveStrength * (1 - waveAge / 0.3) : 1
        flow += dt * (2 + 7 * level) * surge
        if (snakeLoader.item)
            snakeLoader.item.advance(dt)
    }
    Connections {
        target: app
        enabled: vis.running
        function onBeat(strength) { vis.hit(strength) }
    }
    FrameAnimation {
        running: vis.running
        onTriggered: vis.step(Math.min(frameTime, 0.1))
    }
    // A still frame, with motion off or before anything has played, shows a
    // moderate rain rather than an empty field.
    readonly property var shownBands: running || level > 0.02 ? bands : [0.5, 0.55, 0.5, 0.45, 0.45, 0.4, 0.4, 0.4, 0.35, 0.35, 0.3, 0.3, 0.25, 0.25, 0.2, 0.2]

    // One colour from the cover itself, whatever the accent is set to.
    RoundedArt {
        id: sampler
        visible: false; pixels: 48
        source: vis.shown ? (app.current.art || "") : ""
        onReadyChanged: vis.coverSeed = ready ? seedColor() : "transparent"
    }
    property color coverSeed: "transparent"
    readonly property color trailColor: Theme.blend(Theme.primaryContainer, Theme.primary, 0.45)
    readonly property color coverColor: coverSeed.a > 0 ? Theme.blend(coverSeed, Theme.primary, 0.25) : Theme.tertiary
    readonly property color headColor: Theme.primary

    // Where the guarded items are, as fractions of this item. They move only
    // when the layout does, so a few times a second is plenty.
    property var guardRects: []
    function updateGuards() {
        const rects = []
        for (let i = 0; i < guards.length && rects.length < 6; ++i) {
            const g = guards[i]
            if (!g || !g.visible || g.width <= 0 || g.height <= 0 || width <= 0 || height <= 0)
                continue
            const p = g.mapToItem(vis, 0, 0)
            rects.push(Qt.vector4d(p.x / width, p.y / height, g.width / width, g.height / height))
        }
        while (rects.length < 6)
            rects.push(Qt.vector4d(0, 0, 0, 0))
        guardRects = rects
    }
    Timer { interval: 250; repeat: true; running: vis.shown && vis.visible; triggeredOnStart: true; onTriggered: vis.updateGuards() }
    onWidthChanged: updateGuards()
    onHeightChanged: updateGuards()

    Loader {
        anchors.fill: parent
        active: vis.kind === "pixelRain"
        sourceComponent: ShaderEffect {
            objectName: "pixelRain"
            property real flow: vis.flow
            property real cell: 10
            property size size: Qt.size(width, height)
            property vector4d s0: Qt.vector4d(vis.shownBands[0], vis.shownBands[1], vis.shownBands[2], vis.shownBands[3])
            property vector4d s1: Qt.vector4d(vis.shownBands[4], vis.shownBands[5], vis.shownBands[6], vis.shownBands[7])
            property vector4d s2: Qt.vector4d(vis.shownBands[8], vis.shownBands[9], vis.shownBands[10], vis.shownBands[11])
            property vector4d s3: Qt.vector4d(vis.shownBands[12], vis.shownBands[13], vis.shownBands[14], vis.shownBands[15])
            property real wave: vis.running ? vis.waveAge : 10
            property real waveStrength: vis.waveStrength
            property vector4d g0: vis.guardRects[0] || Qt.vector4d(0, 0, 0, 0)
            property vector4d g1: vis.guardRects[1] || Qt.vector4d(0, 0, 0, 0)
            property vector4d g2: vis.guardRects[2] || Qt.vector4d(0, 0, 0, 0)
            property vector4d g3: vis.guardRects[3] || Qt.vector4d(0, 0, 0, 0)
            property vector4d g4: vis.guardRects[4] || Qt.vector4d(0, 0, 0, 0)
            property vector4d g5: vis.guardRects[5] || Qt.vector4d(0, 0, 0, 0)
            property color body: vis.trailColor
            property color cover: vis.coverColor
            property color head: vis.headColor
            fragmentShader: "qrc:/shaders/pixelrain.frag.qsb"
        }
    }

    Loader {
        id: snakeLoader
        anchors.fill: parent
        active: vis.kind === "snake"
        sourceComponent: SnakeField {
            objectName: "snake"
            level: vis.running || vis.level > 0.02 ? vis.level : 0.4
            head: vis.headColor
            body: vis.trailColor
            food: vis.coverColor
            guards: vis.guardRects
        }
    }

    // Development builds only: what the rain is being fed, next to where the
    // song is, to check the two against what is heard.
    Loader {
        active: app.levelOverlay && vis.shown
        x: 24; y: 96; z: 10
        sourceComponent: Rectangle {
            id: overlay
            objectName: "levelOverlay"
            width: 360; height: overlayColumn.implicitHeight + 24
            radius: 12; color: Qt.rgba(0, 0, 0, 0.72)
            property real beatAge: 10
            Connections { target: app; function onBeat() { beatAge = 0 } }
            FrameAnimation { running: vis.running; onTriggered: beatAge += frameTime }
            Shortcut { sequence: "Alt+L"; onActivated: app.visualSync = !app.visualSync }
            Column {
                id: overlayColumn
                x: 12; y: 12; spacing: 6
                Text {
                    color: "white"; font.family: "Menlo"; font.pixelSize: Theme.labelMedium
                    text: "played " + app.position + " ms   heard " + app.heardPosition + " ms\n"
                          + "frame " + Math.floor(app.heardPosition * 30 / 1000)
                          + "   output latency " + app.outputLatencyMs.toFixed(0) + " ms, "
                          + (app.visualSync ? "compensated" : "NOT compensated") + " (⌥L)"
                }
                Row {
                    spacing: 2; height: 60
                    Repeater {
                        model: 16
                        Rectangle {
                            required property int index
                            width: 14; anchors.bottom: parent.bottom
                            height: 2 + 58 * (Number(app.spectrum[index]) || 0)
                            color: index < 3 ? "#ff8a65" : "#90caf9"
                        }
                    }
                    Item { width: 12; height: 1 }
                    Rectangle {
                        width: 40; height: 40; radius: 20; anchors.bottom: parent.bottom
                        color: "#ffeb3b"; opacity: Math.max(0.08, 1 - overlay.beatAge / 0.25)
                        Text { anchors.centerIn: parent; text: "beat"; font.pixelSize: Theme.labelSmall }
                    }
                }
                Row {
                    spacing: 2; height: 30
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            width: 28; anchors.bottom: parent.bottom
                            height: 2 + 28 * (Number(app.audioLevels[index]) || 0)
                            color: "#a5d6a7"
                        }
                    }
                    Text { color: "white"; font.pixelSize: Theme.labelSmall; text: "  meters (5)"; anchors.bottom: parent.bottom }
                }
            }
        }
    }
}
