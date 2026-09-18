import QtQuick
import QtQuick.Controls
TextField {
    id: field
    property string label: ""
    property color labelSurface: Theme.container
    // Material's supporting text sits under the field and explains it; when the
    // field is in error the same line carries the reason and everything the
    // field is drawn with moves to the error role.
    property string supporting: ""
    property string errorText: ""
    readonly property bool errored: errorText.length > 0
    readonly property color accent: errored ? Theme.error : activeFocus ? Theme.primary : Theme.controlOutline
    readonly property bool floatingLabel: activeFocus || length > 0 || preeditText.length > 0
    readonly property bool handlesTextInput: true
    implicitHeight: 56
    leftPadding: 16; rightPadding: 16
    topPadding: 16; bottomPadding: 16
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.bodyLarge
    color: Theme.text; placeholderTextColor: Theme.muted
    selectionColor: Theme.primary; selectedTextColor: Theme.primaryText
    // Material reserves the supporting line so a field does not jump when an
    // error arrives.
    bottomInset: supportLine.visible ? -supportLine.height-4 : 0
    Accessible.name: label || placeholderText
    Accessible.description: errored ? errorText : supporting
    background: Rectangle {
        radius: Theme.shapeExtraSmall; color: "transparent"
        border.width: field.activeFocus || field.errored ? 2 : 1
        border.color: field.accent
        Behavior on border.color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    }
    SungText {
        id: fieldLabel; objectName: "fieldLabel"
        x: field.leftPadding
        y: field.floatingLabel ? -height/2 : (field.height-height)/2
        text: field.label; visible: text.length > 0
        font.pixelSize: field.floatingLabel ? Theme.labelMedium : Theme.bodyLarge
        color: field.errored ? Theme.error : field.activeFocus ? Theme.primary : Theme.muted
        Accessible.ignored: true
        Rectangle { anchors.fill: parent; anchors.leftMargin: -4; anchors.rightMargin: -4; color: field.labelSurface; visible: field.floatingLabel; z: -1 }
        Behavior on y { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
        Behavior on font.pixelSize { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curve } }
    }
    SungText {
        id: supportLine
        objectName: "fieldSupport"
        visible: text.length > 0
        x: field.leftPadding; y: field.height + 4
        width: field.width - field.leftPadding - field.rightPadding
        text: field.errored ? field.errorText : field.supporting
        color: field.errored ? Theme.error : Theme.muted
        font.pixelSize: Theme.labelMedium
        wrapMode: Text.Wrap
        Accessible.ignored: true
    }
}
