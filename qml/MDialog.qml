import QtQuick
import QtQuick.Controls
Dialog {
    id: dialog
    property bool acceptEnabled: true
    property string acceptText: ""
    property Item initialFocus: null
    // M3 dialogs separate scrollable content from the actions with a divider,
    // so a clipped edge reads as "more below" rather than as a cut-off.
    property Flickable scrollSource: null
    readonly property bool moreBelow: !!scrollSource && scrollSource.contentHeight > scrollSource.height+1
                                      && scrollSource.contentY < scrollSource.contentHeight-scrollSource.height-1
    focus: true
    onOpened: if (initialFocus) initialFocus.forceActiveFocus(Qt.TabFocusReason)
    padding: 24
    background: Rectangle {
        color: Theme.container; radius: Theme.shapeExtraLarge
        MElevation { anchors.fill: parent; radius: parent.radius; level: 3 }
    }
    header: Item {
        implicitHeight: Math.max(72, titleLabel.implicitHeight + 48)
        SungText { id: titleLabel; objectName: "dialogTitle"; anchors.verticalCenter: parent.verticalCenter; x: 24; width: parent.width-48; text: dialog.title; font.pixelSize: Theme.headlineSmall; emphasized: true; wrapMode: Text.Wrap; maximumLineCount: 2 }
    }
    footer: DialogButtonBox {
        visible: dialog.standardButtons !== Dialog.NoButton
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        buttonLayout: DialogButtonBox.AndroidLayout
        padding: 24; spacing: 8
        background: Item {
            Rectangle {
                objectName: "dialogScrollDivider"
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 1; color: Theme.outline
                visible: dialog.moreBelow
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            }
        }
        delegate: MButton {
            objectName: dialog.objectName + "_button_" + DialogButtonBox.buttonRole
            readonly property bool confirming: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            ink: Theme.primary; enabled: !confirming || dialog.acceptEnabled
            Component.onCompleted: if (confirming && dialog.acceptText) text = Qt.binding(() => dialog.acceptText)
        }
    }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: app.motion?350:0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastSpatialCurve } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
