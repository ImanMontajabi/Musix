import QtQuick
import QtQuick.Controls
// Material 3 menu.
//
// Two things beyond the standard menu. A vibrant menu takes the tertiary
// container, for one opened over something a surface would disappear into. A
// segmented menu draws its items as one run on a group container of their own,
// which is the shape Material gives a menu that is a choice between peers
// rather than a list of actions.
Menu {
    id: menu

    property bool vibrant: false
    property bool segmented: false

    // The items learn where they sit in the run from the menu, because a
    // MenuItem cannot see its own place in one.
    function restyle() {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (!item || item.segmented === undefined) continue
            item.segmented = menu.segmented
            item.vibrant = menu.vibrant
            item.firstInRun = i === 0
            item.lastInRun = i === menu.count-1
        }
    }
    onCountChanged: Qt.callLater(restyle)
    Component.onCompleted: restyle()

    width: 244; padding: segmented ? 4 : 8; margins: Theme.space12
    height: Math.min(implicitHeight, Math.max(100, (Overlay.overlay ? Overlay.overlay.height : 600)-24))
    delegate: MMenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        spacing: menu.segmented ? Theme.listSegmentedGap : 0
        model: menu.contentModel; currentIndex: menu.currentIndex
        clip: true; boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: MScrollBar { objectName: "menuScrollBar" }
    }
    background: Rectangle {
        // Material puts a menu on the low surface container, whether its items
        // are a run or a list, and a vibrant one on the tertiary container.
        // The plain menu was a step higher, which made it the same surface as
        // the sheets and dialogs it opens over.
        color: menu.vibrant ? Theme.tertiaryContainer : Theme.surfaceLow
        radius: menu.segmented ? Theme.shapeLarge : Theme.shapeLargeIncreased
        border.color: Theme.outlineVariant
        MElevation { anchors.fill: parent; radius: parent.radius; level: 2 }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.enterDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.exitDuration; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.fastEffectsCurve } }
}
