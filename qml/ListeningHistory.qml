import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
MDialog {
    id: dialog
    objectName: "listeningHistoryDialog"
    anchors.centerIn: parent; width: Math.min(460,parent.width-48)
    height: Math.min(parent.height-48, implicitHeaderHeight + implicitFooterHeight + topPadding + bottomPadding + fields.implicitHeight + 8)
    initialFocus: token
    title: "Listening history"; modal: true; standardButtons: Dialog.NoButton
    onAboutToShow: token.clear()
    onClosed: token.clear()
    contentItem: ColumnLayout {
        id: fields; spacing: Theme.space16
        SungText { text: "Send what you play to ListenBrainz, or a server that speaks the same protocol. Paste the user token from your account's settings page; Musix keeps it in a file only you can read."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall }
        MTextField { id: token; objectName: "listeningToken"; Layout.fillWidth: true; label: app.scrobbler.hasToken ? "Replace token" : "User token"; echoMode: TextInput.Password; enabled: !app.scrobbler.busy; onAccepted: if(signInButton.enabled)signInButton.clicked() }
        MSwitch { objectName: "listeningEnabled"; text: "Send my listening history"; visible: app.scrobbler.hasToken; checked: app.scrobbler.enabled; onToggled: app.scrobbler.enabled=checked }
        SungText { objectName: "listeningStatus"; text: app.scrobbler.status + (app.scrobbler.pending>0 ? " · "+app.scrobbler.pending+" waiting" : ""); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall }
        MButton { objectName: "listeningSignOut"; text: "Sign out"; visible: app.scrobbler.hasToken; onClicked: app.scrobbler.signOut() }
    }
    footer: Item {
        implicitHeight: listeningActions.implicitHeight+Theme.dialogPadding
        Row {
            id: listeningActions
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.dialogPadding; spacing: Theme.actionGap
            MButton { text: "Close"; ink: Theme.primary; onClicked: dialog.close() }
            MButton {
                id: signInButton; objectName: "listeningSignIn"; text: "Sign in"; filled: true
                busy: app.scrobbler.busy
                enabled: !app.scrobbler.busy && token.text.trim().length>0
                onClicked: {app.scrobbler.signIn(token.text);token.clear()}
            }
        }
    }
}
