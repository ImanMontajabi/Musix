import QtQuick
import QtQuick.Controls

// Material 3 plain tooltip.
//
// A short label about the control it belongs to, drawn against the theme
// rather than in it: the inverse surface, the ink that goes on it, body small,
// and the smallest corner on the scale. Material does not raise it, so it
// carries no shadow; the rich tooltip is the one that is a surface of its own.
//
// Every plain tooltip in the app is this, so the treatment lives in one place
// rather than being written out again at each control that wants one.
ToolTip {
    id: tip

    // Material publishes no padding for the plain tooltip, so this is ours.
    padding: 8
    contentItem: SungText {
        text: tip.text
        font.pixelSize: Theme.bodySmall
        color: Theme.inverseSurfaceText
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        objectName: "tooltipContainer"
        color: Theme.inverseSurface
        radius: Theme.shapeExtraSmall
    }
}
