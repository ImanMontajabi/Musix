pragma Singleton
import QtQuick

// One place that knows what each shortcut is here, and how to write it down.
//
// Qt maps Ctrl in a key sequence to Command on macOS, so "Ctrl+K" really is
// Cmd+K there -- but a label that still reads "Ctrl+K" is then wrong, which
// is what every tooltip and the whole shortcut sheet used to show. Two of the
// sequences also have to differ, because macOS had already taken them.
QtObject {
    readonly property bool mac: Qt.platform.os==="osx"
    // Cmd+M is Minimize and has to stay Minimize; the app was swallowing it.
    readonly property string miniPlayer: mac ? "Ctrl+Shift+M" : "Ctrl+M"
    // F11 is Show Desktop and never reaches the app at all.
    readonly property string immersive: mac ? "Ctrl+Shift+F" : "F11"
    // Two a Mac user expects that had nothing on them. Left off Linux, where
    // neither is as strong a convention and nothing has changed.
    readonly property string settings: mac ? "Ctrl+," : ""
    readonly property string closeWindow: mac ? "Ctrl+W" : ""
    // Cmd+M is Minimize, and with no menu bar there is no Window menu to
    // carry it, so the app has to do it rather than just stop stealing it.
    readonly property string minimize: mac ? "Ctrl+M" : ""
    // macOS writes modifiers as symbols and runs them together with the key.
    function label(text) {
        if(!mac)return text;
        return text.replace(/Ctrl\+/g,"⌘").replace(/Shift\+/g,"⇧")
                   .replace(/Alt\+/g,"⌥").replace(/Meta\+/g,"⌃")
                   .replace(/\bDelete\b/g,"⌫").replace(/\bEnter\b/g,"↩")
                   .replace(/\bEsc\b/g,"⎋");
    }
}
