import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "shortcutHelp"
    width: fitWidth(540); height: fitHeight(660)
    initialFocus: shortcuts
    title: "Keyboard shortcuts"; modal: true; standardButtons: Dialog.Close
    scrollSource: shortcuts
    contentItem: ListView {
        id: shortcuts; objectName: "shortcutList"; clip: true; spacing: Theme.space4
        boundsBehavior: Flickable.StopAtBounds
        // Written in the portable form and printed through Keymap.label,
        // so macOS reads ⌘⇧M where Linux reads Ctrl+M rather than both being
        // told the Linux answer.
        model: dialog.visible ? [
            ["Search music","Ctrl+K / Ctrl+F"],
            ["Quick actions","Ctrl+Shift+P"],
            ["Show playing song","Ctrl+J"],
            ["Queue","Ctrl+L"],
            ["Lyrics","Ctrl+Y"],
            ["Play / pause¹","Space"],
            ["Previous / next track","Ctrl+← / →"],
            ["Seek 10 seconds¹","← / →"],
            ["Volume¹","Ctrl+↑ / ↓"],
            ["Mute¹","M"],
            ["Back","Alt+←"],
            ["Mini player",Keymap.miniPlayer],
            ["Immersive player",Keymap.immersive],
            ["Select all songs²","Ctrl+A"],
            ["Extend selection²","Shift+↑ / ↓"],
            ["Toggle selection²","Ctrl+Space"],
            ["Play focused song²","Enter"],
            ["Track menu²","Shift+F10"],
            ["Remove selection²","Delete"],
            ["Close / clear selection","Esc"],
            ["Keyboard shortcuts¹","? / F1"]
        ].concat(Keymap.mac ? [["Settings",Keymap.settings],
                               ["Minimize",Keymap.minimize],
                               ["Close window",Keymap.closeWindow]] : [])
          .concat([["Quit","Ctrl+Q"]]) : []
        ScrollBar.vertical: MScrollBar {}
        delegate: RowLayout {
            required property var modelData
            width: shortcuts.width-12; height: Math.max(48,description.implicitHeight+16); spacing: Theme.space16
            SungText { id: description; text: modelData[0]; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: Theme.bodyMedium }
            SungText { text: Keymap.label(modelData[1]); Layout.preferredWidth: 155; horizontalAlignment: Text.AlignRight; color: Theme.primary; font.pixelSize: Theme.labelLarge; labelRole: true }
        }
        footer: SungText { width: shortcuts.width-16; text: "¹ Outside text fields and controls. ² With the song list focused."; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.bodySmall; topPadding: Theme.space16 }
    }
}
