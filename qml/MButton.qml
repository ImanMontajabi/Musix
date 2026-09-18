import QtQuick
import QtQuick.Controls
AbstractButton {
    id: control
    property string symbol: ""
    property string tip: text
    property real contentInset:18
    // M3 button labels are label-large; list rows built from a button use body-large.
    property real labelSize: Theme.labelLarge
    // M3: a trailing icon communicates an action, such as opening something.
    property string trailingSymbol: ""
    property bool leftAligned: false
    property bool filled: false
    property bool tonal: false
    property bool selected: false
    property bool morphPlayback:false
    property bool busy: false
    property bool confirmed: false
    function confirm() {confirmed=true;confirmation.restart();}
    Timer { id: confirmation; interval: 1100; onTriggered: control.confirmed=false }
    onVisibleChanged: if(!visible){confirmation.stop();confirmed=false;}
    readonly property bool needsTooltip: tip.length > 0 && (!text.length || tip !== text || buttonLabel.truncated)
    property color ink: filled ? Theme.primaryText : selected ? Theme.primary : Theme.text
    implicitWidth: text.length ? buttonLabel.implicitWidth + (symbol.length || busy ? 32 : 0) + 36 : 48
    implicitHeight: 48
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled || busy ? 1 : 0.38
    Accessible.name: tip
    Accessible.description: busy ? "Loading" : confirmed ? "Added to queue" : ""
    Loader {
        id: tooltipLoader
        readonly property bool wanted: (control.hovered || control.visualFocus) && control.needsTooltip
        // Keep the popup alive until its exit transition has finished.
        active: false
        function releaseIfIdle() { if (!wanted && (!item || !item.visible)) active=false; }
        onWantedChanged: { if (wanted) active=true; else releaseIfIdle(); }
        Component.onCompleted: if (wanted) active=true
        sourceComponent: ToolTip {
            objectName: "buttonTip"
            parent: control
            visible: tooltipLoader.wanted
            onClosed: Qt.callLater(tooltipLoader.releaseIfIdle)
            delay: 650; text: control.tip
            padding: 10
            contentItem: SungText { text: control.tip; font.pixelSize: 12; color: Theme.background }
            background: Rectangle { color: Theme.text; radius: Theme.shapeSmall }
        }
    }
    background: Rectangle {
        // Material maps buttons to the full shape style, which is half of the
        // shorter side rather than half the height: a narrow button is still a
        // stadium, not an over-rounded lozenge. Pressing morphs it towards a
        // squarer step, which is the shape morph the specification asks for on
        // interaction states.
        radius: control.down ? Theme.shapeMedium
                             : Theme.shapeFull(Math.min(control.width, control.height))
        color: control.filled ? Theme.primary : control.tonal || control.selected ? Theme.high : "transparent"
        border.color: "transparent"
        border.width: 2
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        Behavior on radius { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.8 } }
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: control.ink
            opacity: control.down || control.visualFocus ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
        }
    }
    Rectangle {
        objectName: "buttonFocusRing"
        anchors.fill: parent; anchors.margins: -3
        // The ring sits outside the button, so optical roundness adds the gap
        // between them rather than repeating the button's own radius.
        radius: Theme.shapeInside(Theme.shapeFull(Math.min(width, height)), -3); color: "transparent"
        border.width: 2; border.color: Theme.primary
        visible: control.visualFocus
    }
    contentItem: Item {
        Row {
            id: contentRow; anchors.verticalCenter: parent.verticalCenter; x: control.leftAligned?control.contentInset:(parent.width-width)/2; spacing: 8
            Item {
                width: 24; height: 24; visible: control.symbol.length > 0 || control.busy
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; visible: !control.busy && !playbackGlyph.active; name: control.confirmed?"check":control.symbol; ink: control.ink }
                Loader {id:playbackGlyph;anchors.centerIn:parent;active:control.morphPlayback && !control.busy && !control.confirmed && (control.symbol==="play" || control.symbol==="pause");sourceComponent:PlaybackGlyph {paused:control.symbol==="pause";ink:control.ink}}
                Loader {
                    anchors.fill: parent; active: control.busy
                    sourceComponent: MLoadingIndicator { objectName: "buttonSpinner"; running: control.busy; ink: control.ink; trackColor: "transparent"; label: "Loading"; Accessible.ignored: true }
                }
            }
            SungText { id: buttonLabel; visible: control.text.length > 0; text: control.text; color: control.ink; font.pixelSize: control.labelSize; width: control.leftAligned ? Math.max(0,control.width-(control.symbol.length || control.busy?40:0)-control.contentInset-(control.trailingSymbol.length?36:18)) : implicitWidth; elide: Text.ElideRight; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
        }
        Icon {
            objectName: "buttonTrailingIcon"
            visible: control.trailingSymbol.length > 0
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            name: control.trailingSymbol; ink: control.ink; Accessible.ignored: true
        }
    }
    scale: down ? 0.96 : 1
    Behavior on scale { enabled: app.motion; SpringAnimation { spring: 5; damping: 0.8; mass: 0.6 } }
}
