import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Signing in to Telegram: phone number, the code Telegram sends, and the
// two-step password if the account has one. The steps are Telegram's own, read
// from app.telegram.state; this only shows the one it is waiting for.
MDialog {
    id: dialog
    objectName: "telegramDialog"
    anchors.centerIn: parent; width: Math.min(440, parent.width-48)
    title: "Telegram"; modal: true; standardButtons: Dialog.NoButton
    readonly property string step: app.telegram.state
    readonly property bool waiting: step==="wait-phone" || step==="wait-code" || step==="wait-password"
    initialFocus: step==="wait-code" ? code : step==="wait-password" ? password : phone
    onAboutToShow: { phone.clear(); code.clear(); password.clear(); app.telegram.open() }
    onClosed: { app.telegram.close(); phone.clear(); code.clear(); password.clear() }
    onStepChanged: {
        code.clear(); password.clear()
        if (!visible) return
        if (step==="wait-phone") phone.forceActiveFocus()
        else if (step==="wait-code") code.forceActiveFocus()
        else if (step==="wait-password") password.forceActiveFocus()
    }
    function submit() {
        if (app.telegram.busy) return
        if (step==="wait-phone" && phone.text.trim().length) app.telegram.submitPhone(phone.text)
        else if (step==="wait-code" && code.text.trim().length) app.telegram.submitCode(code.text)
        else if (step==="wait-password" && password.text.length) app.telegram.submitPassword(password.text)
    }
    contentItem: ColumnLayout {
        id: fields; objectName: "telegramFields"; spacing: Theme.space16
        SungText {
            objectName: "telegramPrompt"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
            text: {
                switch (dialog.step) {
                case "no-credentials": return "This build of Musix has no Telegram credentials, so it can’t sign in. A build needs the app’s API id and hash in telegram-api.local.cmake."
                case "starting": return "Connecting to Telegram…"
                case "wait-phone": return "Enter the phone number of your Telegram account. Telegram sends you a code to confirm it’s you."
                case "wait-code": return app.telegram.codeKind==="app" ? "Telegram sent a code to the Telegram app on your other devices."
                                       : app.telegram.codeKind==="sms" ? "Telegram sent the code by text message."
                                       : app.telegram.codeKind==="call" ? "Telegram will call you with the code."
                                       : "Enter the code Telegram sent you."
                case "wait-password": return "This account uses two-step verification. Enter your Telegram password." + (app.telegram.passwordHint ? " Hint: " + app.telegram.passwordHint : "")
                case "ready": return "Signed in as " + app.telegram.name + (app.telegram.username ? " (@" + app.telegram.username + ")" : "") + "."
                default: return ""
                }
            }
        }
        MTextField {
            id: phone; objectName: "telegramPhone"; Layout.fillWidth: true; label: "Phone number"
            visible: dialog.step==="wait-phone"; enabled: !app.telegram.busy
            inputMethodHints: Qt.ImhDialableCharactersOnly; supporting: "With the country code, like +49 151 2345678"
            onAccepted: dialog.submit()
        }
        MTextField {
            id: code; objectName: "telegramCode"; Layout.fillWidth: true; label: "Code"
            visible: dialog.step==="wait-code"; enabled: !app.telegram.busy
            inputMethodHints: Qt.ImhDigitsOnly; maximumLength: 8
            onAccepted: dialog.submit()
        }
        MTextField {
            id: password; objectName: "telegramPassword"; Layout.fillWidth: true; label: "Password"
            visible: dialog.step==="wait-password"; enabled: !app.telegram.busy; echoMode: TextInput.Password
            onAccepted: dialog.submit()
        }
        SungText {
            objectName: "telegramError"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.error
            visible: text.length>0; text: app.telegram.error
        }
        SungText {
            objectName: "telegramNote"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodyMedium
            visible: dialog.waiting || dialog.step==="ready"
            text: "Musix keeps the session on this Mac only, in a file that only you can read, and talks to Telegram directly."
        }
    }
    footer: Item {
        implicitHeight: telegramActions.implicitHeight+Theme.dialogPadding
        Row {
            id: telegramActions
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.dialogPadding; spacing: Theme.actionGap
            MButton {
                objectName: "telegramBack"; text: "Change number"; visible: dialog.step==="wait-code" || dialog.step==="wait-password"
                enabled: !app.telegram.busy; onClicked: app.telegram.back()
            }
            MButton { objectName: "telegramClose"; text: "Close"; ink: Theme.primary; onClicked: dialog.close() }
            MButton {
                objectName: "telegramPrimary"; filled: true; busy: app.telegram.busy
                visible: dialog.waiting || dialog.step==="offline" || dialog.step==="error" || dialog.step==="ready"
                text: dialog.step==="wait-phone" ? "Next" : dialog.step==="wait-code" ? "Confirm" : dialog.step==="wait-password" ? "Sign in"
                      : dialog.step==="ready" ? "Log out" : "Try again"
                enabled: !app.telegram.busy && (dialog.step==="wait-phone" ? phone.text.trim().length>0
                         : dialog.step==="wait-code" ? code.text.trim().length>0
                         : dialog.step==="wait-password" ? password.text.length>0 : true)
                onClicked: dialog.step==="ready" ? app.telegram.logout() : dialog.waiting ? dialog.submit() : app.telegram.retry()
            }
        }
    }
}
