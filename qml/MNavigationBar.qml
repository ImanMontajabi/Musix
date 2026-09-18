import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Material 3 navigation bar.
//
// The specification splits navigation by window size: a bar along the bottom
// for compact windows, a rail beside the content for medium ones. Sung uses the
// rail at its usual sizes and this bar once the window is narrow enough that a
// rail would be taking room the content needs.
//
// Material's rules for it: three to five destinations, never fewer; labels are
// always shown; the active destination carries a filled icon inside an active
// indicator; and the items keep fixed positions.
Rectangle {
    id: bar
    objectName: "navigationBar"

    // Each destination is {key, icon, label}.
    property var destinations: []
    property string current: ""
    signal chosen(string key)

    implicitHeight: 80
    color: Theme.container
    Accessible.role: Accessible.PageTabList
    Accessible.name: "Navigation"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 0
        Repeater {
            model: bar.destinations
            delegate: AbstractButton {
                id: destination
                required property var modelData
                objectName: "navBar_" + modelData.key
                readonly property bool active: bar.current === modelData.key
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                Layout.alignment: Qt.AlignVCenter
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: active
                onClicked: bar.chosen(modelData.key)

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    // The active indicator marks one destination, and only one.
                    Rectangle {
                        objectName: "navBarIndicator_" + destination.modelData.key
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 64; implicitHeight: 32
                        radius: Theme.shapeFull(implicitHeight)
                        color: destination.active ? Theme.primaryContainer
                             : destination.down || destination.visualFocus ? Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.pressedOpacity)
                             : destination.hovered ? Qt.rgba(Theme.primary.r,Theme.primary.g,Theme.primary.b,Theme.hoverOpacity)
                             : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.springFastEffectsMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.springFastEffects } }
                        border.width: destination.visualFocus ? 2 : 0
                        border.color: Theme.primary
                        Icon {
                            anchors.centerIn: parent
                            name: destination.modelData.icon
                            size: 24
                            ink: destination.active ? Theme.containerText : Theme.muted
                        }
                    }
                    // Labels are always shown, never dropped to save room.
                    SungText {
                        objectName: "navBarLabel_" + destination.modelData.key
                        Layout.alignment: Qt.AlignHCenter
                        text: destination.modelData.label
                        font.pixelSize: Theme.labelMedium
                        emphasized: destination.active
                        color: destination.active ? Theme.text : Theme.muted
                    }
                }
            }
        }
    }
}
