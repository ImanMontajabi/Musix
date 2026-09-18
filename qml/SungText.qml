import QtQuick
Text {
    id: label
    objectName: "sungText"
    // Material's emphasized type styles lean on a variable font's weight and
    // width axes to carry hierarchy. Google Sans Flex is variable, so this is
    // the real axis rather than a synthesised bold.
    property bool emphasized: false
    font.family: Theme.fontFamily
    font.variableAxes: ({"wdth": label.emphasized ? Theme.emphasizedWidth : Theme.regularWidth})
    color: Theme.text
    font.pixelSize: 14
    font.weight: emphasized ? Theme.emphasizedWeight : Font.Normal
    textFormat: Text.PlainText
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
