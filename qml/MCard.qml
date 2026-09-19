import QtQuick

// Material 3 card.
//
// Material gives a card three containers to choose between, and the choice says
// how much the card wants to be noticed. An elevated card sits on the low
// surface and casts a shadow; a filled one takes the highest container and
// casts none; an outlined one stays on the low surface and draws a boundary
// instead. All three round at the medium step.
Rectangle {
    id: card

    // "elevated", "filled" or "outlined".
    property string variant: "filled"
    default property alias content: body.data

    radius: Theme.shapeMedium
    // Material's filled card is the highest surface container; the elevated
    // and outlined ones sit lower and are told apart by shadow and boundary.
    // Three containers for three cards: the highest for a filled one, the low
    // container for an elevated one that casts its own shadow, and the surface
    // itself for an outlined one, which is told apart by its boundary.
    color: variant === "filled" ? Theme.highest
         : variant === "outlined" ? Theme.surface : Theme.surfaceLow
    border.width: variant === "outlined" ? 1 : 0
    border.color: Theme.outlineVariant

    MElevation {
        anchors.fill: parent
        radius: parent.radius
        level: card.variant === "elevated" ? 1 : 0
    }
    Item { id: body; anchors.fill: parent }
}
