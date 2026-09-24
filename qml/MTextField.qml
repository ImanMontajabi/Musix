import QtQuick
import QtQuick.Controls
// Material 3 text field.
//
// Two containers to choose between. An outlined field draws a boundary and
// nothing else, which keeps a form of them quiet; a filled one takes the
// highest surface container and rules itself off underneath with an active
// indicator that thickens and takes the accent when it has focus. Material
// reaches for filled where a field is the thing on the surface rather than one
// of several, so a single field in a dialog is a filled one.
TextField {
    id: field
    // "outlined" or "filled".
    property string variant: "outlined"
    readonly property bool filled: variant === "filled"
    property string label: ""
    // The floating label cuts the outline by painting over it, so it has to be
    // the colour of whatever the field sits on: a dialog is not the page.
    property color labelSurface: {
        for (let p = field.parent; p; p = p.parent) {
            const b = p.background
            if (b && b.color !== undefined && b.color.a > 0.99) return b.color
        }
        return Theme.container
    }
    // Material's supporting text sits under the field and explains it; when the
    // field is in error the same line carries the reason and everything the
    // field is drawn with moves to the error role.
    property string supporting: ""
    property string errorText: ""
    readonly property bool errored: errorText.length > 0
    readonly property color accent: errored ? Theme.error : activeFocus ? Theme.primary : Theme.outline
    readonly property bool floatingLabel: activeFocus || length > 0 || preeditText.length > 0
    readonly property bool handlesTextInput: true
    // The container is always 56 high. Supporting text goes under it, outside
    // the frame, and the field grows by that line so a layout makes room for
    // it; the background and the text area are both kept to the container.
    readonly property real containerHeight: 56
    readonly property real supportSpace: supportLine.visible ? supportLine.height + 4 : 0
    implicitHeight: containerHeight + supportSpace
    leftPadding: 16; rightPadding: 16
    // A filled field floats its label inside the container, so the text it
    // labels sits below it rather than in the middle.
    topPadding: field.filled && field.label.length ? 24 : 16
    bottomPadding: (field.filled && field.label.length ? 8 : 16) + supportSpace
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
    color: Theme.text; placeholderTextColor: Theme.placeholder
    selectionColor: Theme.primary; selectedTextColor: Theme.primaryText
    bottomInset: supportSpace
    Accessible.name: label || placeholderText
    Accessible.description: errored ? errorText : supporting
    background: Rectangle {
        objectName: "fieldContainer"
        // A filled field rounds only the corners away from its indicator.
        radius: field.filled ? 0 : Theme.shapeExtraSmall
        topLeftRadius: field.filled ? Theme.shapeExtraSmall : radius
        topRightRadius: topLeftRadius
        color: field.filled ? Theme.highest : "transparent"
        border.width: field.filled ? 0 : field.activeFocus || field.errored ? 2 : 1
        border.color: field.accent
        Behavior on border.color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Rectangle {
            objectName: "fieldIndicator"
            visible: field.filled
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: field.activeFocus || field.errored ? 2 : 1
            color: field.errored ? Theme.error : field.activeFocus ? Theme.primary : Theme.muted
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    SungText {
        id: fieldLabel; objectName: "fieldLabel"
        x: field.leftPadding
        y: field.floatingLabel ? (field.filled ? 8 : -height/2) : (field.containerHeight-height)/2
        text: field.label; visible: text.length > 0
        font.pixelSize: field.floatingLabel ? Theme.labelMedium : Theme.bodyLarge
        color: field.errored ? Theme.error : field.activeFocus ? Theme.primary : Theme.muted
        Accessible.ignored: true
        Rectangle { anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4; color: field.labelSurface; visible: field.floatingLabel && !field.filled; z: -1 }
        Behavior on y { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        Behavior on font.pixelSize { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    }
    SungText {
        id: supportLine
        objectName: "fieldSupport"
        visible: text.length > 0
        x: field.leftPadding; y: field.containerHeight + 4
        width: field.width - field.leftPadding - field.rightPadding
        text: field.errored ? field.errorText : field.supporting
        color: field.errored ? Theme.error : Theme.muted
        font.pixelSize: Theme.labelMedium
        wrapMode: Text.Wrap
        Accessible.ignored: true
    }
}
