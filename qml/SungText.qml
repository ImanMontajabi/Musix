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
    // The role this style is, for the sizes that more than one role shares.
    property string typeRole: ""
    font.family: Theme.fontFamily
    font.variableAxes: ({"wdth": label.emphasized ? Theme.emphasizedWidth : Theme.regularWidth})
    color: Theme.text
    font.pixelSize: 14
    font.weight: Theme.weightFor(emphasized, labelRole)
    // A role carries its line height and letter spacing, not only its size.
    // Letter spacing cannot read the size from a binding: font.letterSpacing
    // and font.pixelSize live in one grouped property, so a binding that sets
    // the first while reading the second re-enters itself. The size is
    // assigned across instead, which settles where a binding would loop.
    property real metricSize: 14
    onFontChanged: if (metricSize !== font.pixelSize) metricSize = font.pixelSize
    Component.onCompleted: metricSize = font.pixelSize
    font.letterSpacing: Theme.trackingFor(metricSize, labelRole, typeRole)
    // A style that wants its own line height states it as a multiple of the
    // size, the way Text takes one, and it is turned into the pixels the
    // fixed mode reads. Left alone, the role's own line height applies.
    property real lineSpacing: 0
    lineHeight: lineSpacing > 0 ? Math.round(font.pixelSize*lineSpacing)
                                : Theme.lineFor(font.pixelSize, labelRole, typeRole)
    lineHeightMode: Text.FixedHeight
    textFormat: Text.PlainText
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
