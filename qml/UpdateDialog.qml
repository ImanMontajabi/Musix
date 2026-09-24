import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What a check for updates found. Only a manual check can end up here as
// "up to date" or an error; an automatic one speaks only when there is a
// version to offer, and not at all about one the person put off or skipped.
MDialog {
    id: dialog
    objectName: "updateDialog"
    modal: true
    width: Math.min(480, parent ? parent.width-48 : 480)
    implicitHeight: header.implicitHeight+contentItem.implicitHeight+footer.implicitHeight+topPadding+bottomPadding
    // "available", "current" or "error".
    property string mode: "available"
    property string version: ""
    property string current: ""
    property string message: ""
    function offer(newer, installed) { mode="available"; version=newer; current=installed; open() }
    function upToDate() { mode="current"; open() }
    function failed(reason) { mode="error"; message=reason; open() }
    title: mode==="available" ? "Musix "+version+" is available — you have "+current
         : mode==="current" ? "You’re up to date" : "Couldn’t check for updates"
    contentItem: SungText {
        objectName: "updateDialogText"
        text: dialog.mode==="available" ? "Download opens its release page on GitHub, with the DMG and what changed. Musix never installs anything by itself."
            : dialog.mode==="current" ? "Musix "+updates.currentVersion+" is the newest version."
            : dialog.message
        wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodyMedium
    }
    footer: Item {
        implicitHeight: 88
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 16; anchors.bottomMargin: 24; spacing: 8
            MButton { objectName: "updateSkip"; visible: dialog.mode==="available"; text: "Skip This Version"; onClicked: { updates.skip(); dialog.close() } }
            Item { Layout.fillWidth: true }
            MButton { objectName: "updateLater"; visible: dialog.mode==="available"; text: "Later"; onClicked: { updates.later(); dialog.close() } }
            MButton { objectName: "updateDownload"; visible: dialog.mode==="available"; text: "Download"; filled: true; onClicked: { updates.openRelease(); dialog.close() } }
            MButton { objectName: "updateOk"; visible: dialog.mode!=="available"; text: "OK"; filled: true; onClicked: dialog.close() }
        }
    }
}
