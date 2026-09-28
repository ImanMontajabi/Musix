import QtQuick

// Material 3 Expressive media controls: previous and next as wide pills either
// side of a wider play button, all one height, as one button group. Previous
// and next sit on the secondary container and play on the primary role, the
// stronger of the two.
//
// Pressing a button widens it and its neighbours give way by the same amount,
// so the group keeps its width and nothing beside it moves. With Animations or
// Reduce motion off the buttons keep their widths.
Item {
    id: group

    property real buttonHeight: 48
    // A narrow bar takes shorter pills rather than dropping a control.
    property bool compact: false
    property real gap: Theme.space4
    property string size: "medium"
    property real iconSize: Theme.buttonIcon[size] || 24
    property string namePrefix: "player"
    property string playName: "playButton"
    // The keyboard shortcut named in each tooltip, where there is one.
    property bool shortcuts: false

    readonly property real sideWidth: Math.round(buttonHeight * (compact ? 1.0 : 1.35))
    readonly property real playWidth: Math.round(buttonHeight * (compact ? 1.35 : 1.8))
    // Material's squish: about a seventh of a pill's width.
    readonly property real grow: Math.round(sideWidth * 0.15)
    readonly property int pressedIndex: !Theme.motion ? -1 : previousButton.down ? 0 : playButton.down ? 1 : nextButton.down ? 2 : -1
    function widthOf(index, base) {
        if (pressedIndex < 0)
            return base
        if (index === pressedIndex)
            return base + grow
        if (Math.abs(index - pressedIndex) !== 1)
            return base
        // The middle button pushes on both sides, an end one on one.
        return base - (pressedIndex === 1 ? grow / 2 : grow)
    }

    implicitWidth: sideWidth * 2 + playWidth + gap * 2
    implicitHeight: Math.max(Theme.minimumTarget, buttonHeight)

    component GroupButton: MButton {
        size: group.size
        containerHeight: group.buttonHeight
        sizedIcon: group.iconSize
        implicitHeight: group.implicitHeight
        stretch: true
        squish: true
        y: (group.height - height) / 2
        Behavior on width { enabled: Theme.motion; SpringAnimation { spring: 6; damping: 0.55; mass: 0.6; epsilon: 0.25 } }
    }

    GroupButton {
        id: previousButton
        objectName: group.namePrefix + "Previous"
        x: 0
        width: group.widthOf(0, group.sideWidth)
        tonal: true
        symbol: "previous"
        tip: "Previous" + (group.shortcuts ? " · " + Keymap.label("Ctrl+←") : "")
        enabled: app.queue.count > 0
        onClicked: app.previous()
    }
    GroupButton {
        id: playButton
        objectName: group.playName
        x: previousButton.x + previousButton.width + group.gap
        width: group.widthOf(1, group.playWidth)
        filled: true
        morphPlayback: true
        playingRadius: Math.round(group.buttonHeight * 0.3)
        busy: app.buffering
        symbol: app.playing || app.resolving ? "pause" : "play"
        tip: (app.playing || app.resolving ? "Pause" : "Play") + (group.shortcuts ? " · Space" : "")
        enabled: app.queue.count > 0
        onClicked: app.toggle()
    }
    GroupButton {
        id: nextButton
        objectName: group.namePrefix + "Next"
        x: playButton.x + playButton.width + group.gap
        width: group.widthOf(2, group.sideWidth)
        tonal: true
        symbol: "next"
        tip: "Next" + (group.shortcuts ? " · " + Keymap.label("Ctrl+→") : "")
        enabled: app.queue.count > 0
        onClicked: app.next()
    }
}
