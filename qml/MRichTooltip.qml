import QtQuick
import QtQuick.Controls

// Material 3 rich tooltip.
//
// A plain tooltip names a control. A rich one explains it: a title-small
// subhead, body-medium supporting text and, where there is somewhere to go, an
// action in the primary role. Material gives it a surfaceContainer background
// at a medium corner and two levels of elevation, and, unlike the plain form,
// keeps it on screen long enough to be read.
ToolTip {
    id: tip

    property string subhead: ""
    property string supporting: ""
    property string actionText: ""
    signal actionTriggered()

    // Material holds a rich tooltip until it is dismissed rather than timing it
    // out from under the reader.
    delay: 500
    timeout: -1
    padding: Theme.space16
    implicitWidth: Math.min(312, Math.max(200, bodyColumn.implicitWidth + padding*2))

    contentItem: Column {
        id: bodyColumn
        // A ToolTip is a Popup, not an Item, so the attached property only
        // takes on what it shows.
        Accessible.role: Accessible.ToolTip
        Accessible.name: tip.subhead
        Accessible.description: tip.supporting
        spacing: Theme.space4
        SungText {
            objectName: "richTooltipSubhead"
            visible: tip.subhead.length > 0
            width: Math.min(280, implicitWidth)
            text: tip.subhead
            color: Theme.muted
            font.pixelSize: Theme.labelLarge
            emphasized: true
            wrapMode: Text.Wrap
        }
        SungText {
            objectName: "richTooltipBody"
            visible: tip.supporting.length > 0
            width: Math.min(280, implicitWidth)
            text: tip.supporting
            color: Theme.muted
            font.pixelSize: Theme.bodyMedium
            wrapMode: Text.Wrap
        }
        MButton {
            objectName: "richTooltipAction"
            visible: tip.actionText.length > 0
            text: tip.actionText
            ink: Theme.primary
            contentInset: 0
            leftAligned: true
            implicitWidth: Math.max(48, contentWidth)
            onClicked: { tip.actionTriggered(); tip.close() }
        }
    }
    background: Rectangle {
        color: Theme.container
        radius: Theme.shapeMedium
        border.width: 1; border.color: Theme.outlineVariant
        MElevation { anchors.fill: parent; radius: parent.radius; level: 2 }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
