import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog
    objectName: "smartPlaylistDialog"
    property string playlistId: ""
    property string sourceFilter: "any"
    property int daysFilter: 0
    title: playlistId ? "Edit rules" : "New smart playlist"
    width: fitWidth(480); height: fitHeight(720)
    modal: true; standardButtons: Dialog.Cancel | Dialog.Ok; acceptText: "Save"
    initialFocus: nameField; acceptEnabled: nameField.text.trim().length>0
    scrollSource: scroll.contentItem
    function edit(id) {
        const p=app.smartPlaylist(id), rules=p.rules || {};
        playlistId=id; nameField.text=p.title || ""; artistField.text=rules.artist || "";
        titleField.text=rules.title || ""; albumField.text=rules.album || ""; sourceFilter=rules.source || "any";
        yearFrom.text=rules.yearFrom ? String(rules.yearFrom) : ""; yearTo.text=rules.yearTo ? String(rules.yearTo) : "";
        minutesFrom.text=rules.minSeconds ? String(Math.round(rules.minSeconds/60)) : "";
        minutesTo.text=rules.maxSeconds ? String(Math.round(rules.maxSeconds/60)) : "";
        daysFilter=rules.days || 0; likedSwitch.checked=!!rules.likedOnly; open();
    }
    onAccepted: {
        const id=app.saveSmartPlaylist(playlistId,nameField.text,{
            artist:artistField.text,title:titleField.text,album:albumField.text,
            source:sourceFilter,days:daysFilter,likedOnly:likedSwitch.checked,
            yearFrom:parseInt(yearFrom.text) || 0,yearTo:parseInt(yearTo.text) || 0,
            minSeconds:(parseInt(minutesFrom.text) || 0)*60,maxSeconds:(parseInt(minutesTo.text) || 0)*60});
        if(id)app.openPlaylist(id);
    }
    ScrollView {
        id: scroll; objectName: "smartScroll"; anchors.fill: parent; clip: true; contentWidth: availableWidth; contentHeight: fields.implicitHeight; rightPadding: 10
        ScrollBar.vertical: MScrollBar { parent: scroll; x: scroll.width-width; height: scroll.availableHeight; orientation: Qt.Vertical }
        ColumnLayout {
            id: fields; width: scroll.availableWidth; spacing: 12
            SungText { text: "Name" }
            MTextField { id: nameField; objectName: "smartName"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Playlist name" }
            SungText { text: "Artist contains" }
            MTextField { id: artistField; objectName: "smartArtist"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Any artist" }
            SungText { text: "Title contains" }
            MTextField { id: titleField; objectName: "smartTitle"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Any title" }
            SungText { text: "Album contains" }
            MTextField { id: albumField; objectName: "smartAlbum"; Layout.fillWidth: true; maximumLength: 120; placeholderText: "Any album" }
            SungText { text: "Released between" }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                MTextField { id: yearFrom; objectName: "smartYearFrom"; Layout.fillWidth: true; maximumLength: 4; placeholderText: "Any year"; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 0; top: 9999 } }
                SungText { text: "and"; color: Theme.muted }
                MTextField { id: yearTo; objectName: "smartYearTo"; Layout.fillWidth: true; maximumLength: 4; placeholderText: "Any year"; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 0; top: 9999 } }
            }
            SungText { text: "Length in minutes" }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                MTextField { id: minutesFrom; objectName: "smartMinutesFrom"; Layout.fillWidth: true; maximumLength: 3; placeholderText: "Any length"; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 0; top: 600 } }
                SungText { text: "to"; color: Theme.muted }
                MTextField { id: minutesTo; objectName: "smartMinutesTo"; Layout.fillWidth: true; maximumLength: 3; placeholderText: "Any length"; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 0; top: 600 } }
            }
            RowLayout {
                Layout.fillWidth: true
                SungText { text: "Source"; Layout.fillWidth: true }
                MButton { objectName: "smartSource"; text: ({any:"Any source",local:"Local files",youtube:"YouTube Music",subsonic:"Music server"})[dialog.sourceFilter]; tonal: true; onClicked: sources.popup(this,0,height) }
            }
            RowLayout {
                Layout.fillWidth: true
                SungText { text: "Last played"; Layout.fillWidth: true }
                MButton { objectName: "smartPlayed"; text: dialog.daysFilter===0?"Any time":dialog.daysFilter===-1?"Never":"Over "+dialog.daysFilter+" days ago"; tonal: true; onClicked: played.popup(this,0,height) }
            }
            MSwitch { id: likedSwitch; objectName: "smartLiked"; text: "Liked songs only" }
            SungText { text: "Matches saved music. All rules apply. A year or length rule skips songs that have no year or duration."; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    MMenu { id: sources; Repeater { model: [{key:"any",title:"Any source"},{key:"local",title:"Local files"},{key:"youtube",title:"YouTube Music"},{key:"subsonic",title:"Music server"}]; MMenuItem { required property var modelData; text: modelData.title; checkable: true; checked: dialog.sourceFilter===modelData.key; onTriggered: dialog.sourceFilter=modelData.key } } }
    MMenu { id: played; Repeater { model: [0,-1,7,30,90,365]; MMenuItem { required property int modelData; text: modelData===0?"Any time":modelData===-1?"Never":"Over "+modelData+" days ago"; checkable: true; checked: dialog.daysFilter===modelData; onTriggered: dialog.daysFilter=modelData } } }
}
