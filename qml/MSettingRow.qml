import QtQuick
import QtQuick.Layouts

// A settings list row. M3 asks that primary text sit in the same position in
// every list item, so the label is anchored to the leading edge and sized like
// the switch labels it sits beside, not like a button.
MButton {
    // A row that opens something says so with a trailing icon; a row that acts
    // immediately does not, so the two read differently at a glance.
    property bool opens: false
    trailingSymbol: opens ? "chevron" : ""
    leftAligned: true
    // Material's list item padding, inside the highlight. The row reaches out
    // by the same amount on either side, so its label stays on the text edge
    // every other row in the column shares while the highlight is the full
    // width of the list.
    contentInset: Theme.listItemPadding
    rightPadding: Theme.listItemPadding
    labelSize: Theme.bodyLarge
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.leftMargin: -Theme.listItemPadding
    Layout.rightMargin: -Theme.listItemPadding
}
