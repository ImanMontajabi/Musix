import QtQuick
import QtQuick.Controls
Slider {
    id: slider
    readonly property bool handlesArrowKeys: true
    // Material shows the value in a label above the handle while the slider is
    // being moved, so the number is where the eye already is.
    property string valueLabel: ""
    implicitHeight: 40
    readonly property real thumbCenter: visualPosition * (availableWidth - 4) + 2
    background: Item {
        x: slider.leftPadding; y: slider.topPadding+(slider.availableHeight-height)/2
        width: slider.availableWidth; height: 4
        Rectangle { objectName: "sliderActiveTrack"; width: Math.max(0, slider.thumbCenter-8); height: 4; radius: Theme.shapeFull(4); color: Theme.primary }
        Rectangle { objectName: "sliderInactiveTrack"; x: Math.min(parent.width, slider.thumbCenter+8); width: parent.width-x; height: 4; radius: Theme.shapeFull(4); color: Theme.high }
        Rectangle { x: parent.width-4; width: 4; height: 4; radius: Theme.shapeFull(4); color: Theme.muted; visible: slider.thumbCenter+8<parent.width-4 }
    }
    handle: Rectangle {
        x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width)
        y: slider.topPadding+(slider.availableHeight-height)/2
        width: 4; height: slider.pressed?30:24; radius: Theme.shapeFull(4); color: Theme.primary
        border.width: slider.visualFocus?2:0; border.color: Theme.text
        Behavior on height { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; epsilon: 0.1 } }
    }
    ToolTip {
        objectName: "sliderValueLabel"
        visible: slider.valueLabel.length > 0 && (slider.pressed || slider.visualFocus)
        delay: 0; timeout: -1
        x: slider.leftPadding + slider.visualPosition*(slider.availableWidth-width)
        y: -height - 4
        padding: 8
        contentItem: SungText { objectName: "sliderValueText"; text: slider.valueLabel; color: Theme.inverseSurfaceText; font.pixelSize: Theme.labelLarge; labelRole: true }
        background: Rectangle { color: Theme.inverseSurface; radius: Theme.shapeFull(height) }
    }
}
