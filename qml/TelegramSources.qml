import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What to listen to on Telegram: Saved Messages, a chat or channel the account
// has joined (a forum's topics one by one), or a public channel by @name or
// link. Each choice becomes a playlist of its songs in the library; this is
// where they are chosen, refreshed and removed.
MDialog {
    id: dialog
    objectName: "telegramSourcesDialog"
    anchors.centerIn: parent
    width: Math.min(560, parent.width-48); height: Math.min(parent.height-48, 680)
    title: "Telegram music"; modal: true; standardButtons: Dialog.NoButton
    readonly property var tg: app.telegram
    property string mode: "yours"
    // The forum whose topics are on show, or null for the list of chats.
    property var forum: null
    property string filter: ""
    readonly property var shownChats: {
        const needle = filter.trim().toLowerCase()
        return tg.chats.filter(c => !needle || c.title.toLowerCase().indexOf(needle) >= 0)
    }
    // Reads tg.sources so that a row's "added" state follows the list.
    function added(chatId, topicId) { return tg.sources.length >= 0 && tg.hasSource(chatId, topicId || "") }
    function choose(next) {
        mode = next; forum = null; tg.clearTopics()
        if (next === "chats" && tg.chats.length === 0 && !tg.listing) tg.loadChats()
    }
    function openForum(chat) { forum = chat; tg.loadTopics(chat.chatId) }
    function subtitle(source) {
        const status = source.status
        if (status === "queued") return "Waiting for Telegram…"
        if (status === "syncing") return source.count + " songs so far…"
        if (status === "waiting") return source.message
        if (status === "error") return source.message
        const synced = source.syncedAt > 0 ? "synced " + Qt.formatDateTime(new Date(source.syncedAt*1000), "d MMM yyyy, HH:mm") : "not synced yet"
        return source.count + " songs · " + synced + (source.message ? " · " + source.message : "")
    }
    function kindLabel(kind) {
        return kind === "saved" ? "Your own messages" : kind === "channel" ? "Channel" : kind === "forum" ? "Group with topics" : kind === "topic" ? "Topic" : "Group"
    }
    onAboutToShow: {
        filter = ""; forum = null; tg.open()
        mode = tg.sources.length > 0 ? "yours" : "chats"
        if (mode === "chats") tg.loadChats()
    }
    onClosed: { tg.clearTopics(); tg.clearFound(); tg.close() }

    component Row3: RowLayout {
        id: row
        property string title
        property string subtitle
        property bool problem: false
        property bool added: false
        property bool hasTopics: false
        property bool addable: true
        signal add()
        signal topics()
        width: ListView.view ? ListView.view.width : implicitWidth
        height: Math.max(56, implicitHeight)
        spacing: Theme.space8
        ColumnLayout {
            Layout.fillWidth: true; spacing: 0
            SungText { text: row.title; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: Theme.bodyLarge }
            SungText {
                text: row.subtitle; visible: text.length > 0; Layout.fillWidth: true; elide: Text.ElideRight
                font.pixelSize: Theme.bodyMedium; color: row.problem ? Theme.error : Theme.muted
            }
        }
        MButton { objectName: "telegramTopics"; text: "Topics"; visible: row.hasTopics; onClicked: row.topics() }
        MButton {
            objectName: "telegramAdd"; filled: !row.added; text: row.added ? "Added" : "Add"
            visible: row.addable; enabled: !row.added; onClicked: row.add()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.space12
        SungText {
            objectName: "telegramSignInFirst"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
            visible: !dialog.tg.signedIn
            text: "Sign in to Telegram first, under Settings → Connections → Telegram."
        }
        MSegmentedControl {
            objectName: "telegramSourceMode"; accessibleName: "What to show"; Layout.fillWidth: true
            visible: dialog.tg.signedIn
            options: [{key: "yours", label: "Chosen", name: "telegramModeYours"}, {key: "chats", label: "Your chats", name: "telegramModeChats"},
                      {key: "search", label: "Search", name: "telegramModeSearch"}]
            value: dialog.mode; onChosen: value => dialog.choose(value)
        }

        // Chosen sources
        ColumnLayout {
            visible: dialog.tg.signedIn && dialog.mode === "yours"; Layout.fillWidth: true; Layout.fillHeight: true; spacing: Theme.space8
            RowLayout {
                Layout.fillWidth: true; visible: dialog.tg.sources.length > 0
                SungText { Layout.fillWidth: true; color: Theme.muted; text: "Each one is a playlist in your library." }
                MButton { objectName: "telegramRefreshAll"; text: "Refresh all"; symbol: "refresh"; onClicked: dialog.tg.refreshAll() }
            }
            SungText {
                objectName: "telegramNoSources"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
                visible: dialog.tg.sources.length === 0
                text: "Nothing chosen yet. Pick Saved Messages, a chat or a channel under Your chats, or find a public channel by its @name."
            }
            ListView {
                id: sourceList; objectName: "telegramSourceList"
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: dialog.tg.sources; spacing: 0
                delegate: RowLayout {
                    id: source
                    required property var modelData
                    readonly property bool busy: modelData.status === "queued" || modelData.status === "syncing" || modelData.status === "waiting"
                    objectName: "telegramSource_" + modelData.key
                    width: ListView.view.width; height: 60; spacing: Theme.space4
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        SungText { text: source.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: Theme.bodyLarge }
                        SungText {
                            objectName: "telegramSourceStatus"
                            text: dialog.subtitle(source.modelData); Layout.fillWidth: true; elide: Text.ElideRight
                            font.pixelSize: Theme.bodyMedium; color: source.modelData.status === "error" ? Theme.error : Theme.muted
                        }
                    }
                    MButton {
                        objectName: "telegramRefresh"; symbol: source.busy ? "close" : "refresh"; tip: source.busy ? "Stop" : "Refresh"
                        onClicked: source.busy ? dialog.tg.cancelSync(source.modelData.key) : dialog.tg.refresh(source.modelData.key)
                    }
                    MButton { objectName: "telegramRemove"; symbol: "trash"; tip: "Remove, with its playlist"; onClicked: dialog.tg.removeSource(source.modelData.key) }
                }
            }
        }

        // The account's chats, or one forum's topics
        ColumnLayout {
            visible: dialog.tg.signedIn && dialog.mode === "chats"; Layout.fillWidth: true; Layout.fillHeight: true; spacing: Theme.space8
            RowLayout {
                Layout.fillWidth: true; visible: dialog.forum !== null
                MButton { objectName: "telegramForumBack"; symbol: "back"; tip: "Back to your chats"; onClicked: { dialog.forum = null; dialog.tg.clearTopics() } }
                SungText { Layout.fillWidth: true; text: dialog.forum ? dialog.forum.title : ""; elide: Text.ElideRight; font.pixelSize: Theme.bodyLarge }
            }
            MTextField {
                id: chatFilter; objectName: "telegramChatFilter"; Layout.fillWidth: true; label: "Filter chats"
                visible: dialog.forum === null; text: dialog.filter; onTextEdited: dialog.filter = text
            }
            SungText {
                objectName: "telegramListStatus"; Layout.fillWidth: true; wrapMode: Text.Wrap
                visible: text.length > 0; color: dialog.tg.listError ? Theme.error : Theme.muted
                text: dialog.tg.listError || (dialog.tg.listing ? "Loading from Telegram…" : "")
            }
            ListView {
                id: chatList; objectName: "telegramChatList"
                visible: dialog.forum === null; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: dialog.shownChats
                delegate: Row3 {
                    required property var modelData
                    objectName: "telegramChat_" + modelData.chatId
                    title: modelData.title; subtitle: dialog.kindLabel(modelData.kind) + (modelData.username ? " · @" + modelData.username : "")
                    hasTopics: modelData.kind === "forum"
                    added: dialog.added(modelData.chatId)
                    onAdd: dialog.tg.addSource(modelData.chatId, modelData.title, modelData.kind, modelData.username)
                    onTopics: dialog.openForum(modelData)
                }
            }
            ListView {
                id: topicList; objectName: "telegramTopicList"
                visible: dialog.forum !== null; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: dialog.forum ? [{topicId: "0", title: "All topics"}].concat(dialog.tg.topics) : []
                delegate: Row3 {
                    required property var modelData
                    objectName: "telegramTopic_" + modelData.topicId
                    title: modelData.title; subtitle: modelData.topicId === "0" ? "Every song in the group" : ""
                    added: dialog.forum ? dialog.added(dialog.forum.chatId, modelData.topicId) : false
                    onAdd: dialog.tg.addSource(dialog.forum.chatId, dialog.forum.title, dialog.forum.kind, dialog.forum.username,
                                              modelData.topicId === "0" ? "" : modelData.topicId, modelData.title)
                }
            }
        }

        // A public channel
        ColumnLayout {
            visible: dialog.tg.signedIn && dialog.mode === "search"; Layout.fillWidth: true; Layout.fillHeight: true; spacing: Theme.space8
            RowLayout {
                Layout.fillWidth: true
                MTextField {
                    id: query; objectName: "telegramQuery"; Layout.fillWidth: true; label: "@name or t.me link"
                    onAccepted: if (text.trim().length) dialog.tg.search(text)
                }
                MButton {
                    objectName: "telegramFind"; text: "Find"; filled: true; enabled: query.text.trim().length > 0 && !dialog.tg.listing
                    onClicked: dialog.tg.search(query.text)
                }
            }
            SungText {
                objectName: "telegramSearchStatus"; Layout.fillWidth: true; wrapMode: Text.Wrap
                visible: text.length > 0; color: dialog.tg.listError ? Theme.error : Theme.muted
                text: dialog.tg.listError || (dialog.tg.listing ? "Looking it up…" : "")
            }
            Row3 {
                objectName: "telegramFound"; Layout.fillWidth: true
                visible: !!dialog.tg.found.chatId
                title: dialog.tg.found.title || ""
                subtitle: dialog.kindLabel(dialog.tg.found.kind) + (dialog.tg.found.username ? " · @" + dialog.tg.found.username : "")
                hasTopics: dialog.tg.found.kind === "forum"
                added: dialog.tg.found.chatId ? dialog.added(dialog.tg.found.chatId) : false
                onAdd: dialog.tg.addSource(dialog.tg.found.chatId, dialog.tg.found.title, dialog.tg.found.kind, dialog.tg.found.username)
                onTopics: { dialog.mode = "chats"; dialog.openForum(dialog.tg.found) }
            }
            Item { Layout.fillHeight: true }
        }
    }
    footer: Item {
        implicitHeight: sourcesActions.implicitHeight+Theme.dialogPadding
        Row {
            id: sourcesActions
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.dialogPadding; spacing: Theme.actionGap
            MButton { objectName: "telegramSourcesClose"; text: "Close"; ink: Theme.primary; onClicked: dialog.close() }
        }
    }
}
