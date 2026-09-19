import QtQuick
import QtQuick.Controls
MenuSeparator {
    // Material rules a menu off edge to edge and a list with 16dp kept clear
    // at both ends, so the rule starts where the labels start.
    property real inset: 0
    topPadding: 6; bottomPadding: 6; leftPadding: inset; rightPadding: inset
    contentItem: Rectangle { implicitWidth: 220; implicitHeight: 1; color: Theme.outlineVariant }
}
