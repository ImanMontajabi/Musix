import QtQuick
Text {
    id: label
    objectName: "sungText"
    // Material's emphasized type styles lean on a variable font's weight and
    // width axes to carry hierarchy. Google Sans Flex is variable, so this is
    // the real axis rather than a synthesised bold. The step Material takes
    // depends on the role: a label goes from medium to bold, and every other
    // role from regular to medium, so the style has to say which it is.
    property bool emphasized: false
    property bool labelRole: false
    font.family: Theme.fontFamily
    font.variableAxes: ({"wdth": label.emphasized ? Theme.emphasizedWidth : Theme.regularWidth})
    color: Theme.text
    font.pixelSize: 14
    font.weight: Theme.weightFor(emphasized, labelRole)
    textFormat: Text.PlainText
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
