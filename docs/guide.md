# Musix guide

Everything the app can do, for macOS. The [README](../README.md) covers
installing and the shortcuts you need on day one.


## Music library

Search for music or paste a YouTube song or playlist link. Use **Library → Local files → +** to add files, or **Folders → Add folder…** for a whole music folder. Enter its absolute path (or `~/Music`), or use **Browse…**, then choose **Add folder**. This also works for network shares mounted as local folders and does not depend on the system folder picker. Subfolders are scanned recursively. Saved folders update automatically while Musix is running. Settings can disable automatic updates; the refresh button also rescans them. Missing files remain listed as unavailable; files and playlist entries are never deleted by a scan. Folder monitoring uses filesystem notifications and is bounded to 4,096 directories and 10,000 audio files; use manual rescan for larger libraries or mounts that do not deliver notifications.

In **Local files**, open **Find and sort songs → Folder** to group songs by their parent directory, with natural filename order inside each group. The filter also searches folder paths.

With a song list focused, start typing to jump to the first matching title, or artist when no title matches. The search refines as you type and clears after a short pause. Turn it off with **Settings → Library → Type to jump in lists**.

Create an automatic playlist from **Library → Playlists → Smart playlist**. Combine artist, title, album, release-year range, length range, source, liked status and last-played rules over your saved music. A year or length rule skips songs with no year or duration, and a range that ends before it starts drops its upper bound. Use **Edit rules** to change it; matching songs update automatically.

**Local files → Albums / Artists** groups imported music by its tags. Albums use album-artist tags when present, with disc and track order preserved. Use **Rescan** after upgrading to refresh tags on existing imports. Folder-sorted songs and multi-disc albums have collapsible group headings with track counts and group-play buttons. Collapsed songs stay in the collection but are excluded from selection.

**Library → Playlists → Import M3U** reads an `.m3u` or `.m3u8` file, imports the audio it names and saves it as a playlist. Relative entries resolve against the playlist file’s own folder. A playlist’s menu offers **Export as M3U…** in return. Only local files can travel this way: streaming ids mean nothing to other players, and server URLs would carry credentials that library exports deliberately leave out, so those songs are skipped and counted.

In a playlist’s menu, choose **Change cover…** to crop a PNG, JPEG or WebP. Musix saves a 512px copy; the original stays untouched. **Restore cover collage** returns to automatic artwork.

## Artwork and appearance

The navigation rail on the left can expand. Use the menu button at its top to switch between icons and a wider list that also shows your pinned collections. The choice is remembered, and windows narrower than 1080px stay collapsed because the expanded rail sits beside the content rather than over it.

On first run Musix offers a three-step setup: theme and accent color, a music folder, and the page to open on. Every step can be skipped, and each control also lives in Settings.

Open **Home → Customize Home** to reorder or hide sections; **Reset layout** restores them. **Settings → Library → Start page** chooses Home, Local, Server or Liked for future launches. Direct launch links still take priority.

**Settings → Appearance → Current view layout** saves a density override for the current view. Local album/artist browsers and the playlist overview also offer Grid / List. Choose Default density to follow the global setting. Up to 64 view preferences are retained locally.

**Settings → Appearance → Density** switches between comfortable and compact track rows and album grids without changing font size. Density changes animate when motion is enabled. Opening an album carries its cover into the header; Back returns it to the originating card when visible. The header contracts as you scroll while keeping playback actions available.

For animated artwork, place a **GIF, animated WebP, MP4 or WebM** beside your music, named `cover`, `folder`, `front` or `artwork` (for example, `cover.mp4`). A matching song filename, such as `Song.gif` beside `Song.flac`, takes priority. Names are case-insensitive. JPG, PNG and static WebP sidecars also work as still covers. Import or rescan the folder after changing its artwork. Covers are limited to 128 MiB and 4096 × 4096 pixels; unreadable covers fall back to embedded artwork. Animation is silent, pauses with playback, and respects **Settings → Appearance → Animations** and **Animated album artwork**. No artwork service account is needed.

A song that is a music video or an upload has a video frame for a cover rather than album art. Wherever that cover is drawn larger than a list row, Musix asks YouTube for the 1280 × 720 frame and keeps the small one only when the upload has no HD version.

**Settings → Appearance → Album covers for music videos** goes further and looks for the album's own cover on Apple Music's public pages, using the same unofficial, best-effort match as animated covers. When Apple has no album for a song, which is common for game soundtracks, fan uploads and releases that never reached a store, MusicBrainz and its Cover Art Archive are asked next; those covers are scans people uploaded, so anything below 500 pixels is left alone, because a thumbnail is no improvement on a video frame. The setting is on by default and works whether or not animations are enabled. The cover replaces the frame everywhere, including the desktop's own media controls. A song neither service has keeps its frame, and is not looked up again; **Find cover again** in the artwork controls asks once more. Up to 2,000 answers are kept locally.

For YouTube songs, **Online animated covers** looks for a matching album on Apple Music’s public pages. This unofficial, best-effort lookup needs no account; it sends the song’s title and artist to Apple, and to MusicBrainz when a cover is wanted and Apple has none, and checks the album and duration when available. Singles can use artwork from a verified original album release; missing search results are checked against the album’s track list. Many albums have no animation, and uncertain matches keep the original still cover. Temporary lookup failures get one automatic retry. Downloads are limited to 16 MiB per silent cover and 64 MiB of disk cache. Disable the lookup in Settings or remove downloaded covers with **Clear cache**.

Open **Settings → Appearance → Current artwork** to preview the current cover, view its source album, retry a match, disable animation for that song or choose a local GIF, WebP, MP4 or WebM. Local choices are saved per song and reference the selected file; keep it in place. **Use automatic cover** clears the override.

**Settings → Appearance → Use artwork accent** colors controls from the current cover. It is off by default. Monochrome or missing covers use the normal theme.

**Settings → Appearance → Accent color** picks a Material source color for buttons, highlights and progress. Musix solves each seed against the current surfaces, so the resulting color always clears 4.5:1 contrast in both themes. **Default** restores the built-in palette. Artwork accent takes priority while it is on.

**Settings → Appearance → Ambient artwork backdrop** draws the current cover, softened and dimmed, behind Home, the immersive player and the Now playing panel. Home has no cover of its own, so it borrows the playing track’s, or the first artwork on its shelves when nothing is playing. It is on by default. The cover is decoded small and blurred once when it loads, so the wash costs one small texture and no per-frame effect, and a scrim keeps text contrast unchanged. In the immersive player the backdrop drifts slowly and, with **Backdrop follows the music**, swells gently with the decoded low end of the track; it holds still on rounded surfaces such as Home and the Now playing panel, because swelling there would square off their corners. Everything stops when **Animations** is off. Hiding either surface releases the decoded cover.

The artwork controls also offer **Fit / Fill**, remembered per album where album metadata is available, otherwise per song. Immersive artwork requests a display-sized still cover up to 1600px; source quality remains the limit. Artwork accents transition smoothly when animations are enabled. Next and Previous move song information in opposite directions. Player covers crossfade between songs; transitions stop when hidden or animations are disabled.

Click album or immersive artwork to inspect the full cover. Use the wheel or + / − to zoom, 0 to reset, and Escape to close. The viewer uses available source detail, capped at 1600px. You can also click the preview in **Current artwork**.

## Playback and shortcuts

Press **⌘⇧F** for immersive playback. The **…** menu selects Artwork, Lyrics or Split; **⌘L** opens the queue. Optional **Auto-hide controls** fades controls while idle; pointer or keyboard activity restores them. The cursor stays visible. Click an available artist or album name to browse, then use Back to return.

The same **…** menu offers **Up next covers**: a carousel of the queue below the player, with the playing track centered and large and the rest peeking either side. Scrolling snaps to a cover and plays it as soon as it settles; clicking a cover plays it directly. The choice is remembered. **Show all** opens the full queue for anything the strip cannot reach. The carousel shrinks the main cover to make room, so it is off by default.

The sleep timer can stop at the **end of the queue** as well as after a set time or the current track. It is offered only when the queue can actually finish, so it is unavailable while shuffle or repeat is on.

**Settings → Playback → Resume long recordings** returns to where you left a recording of 20 minutes or more: mixes, sets and live shows. The mark is written when you pause or move on, dropped once the recording finishes or if you stop near either end, and up to 400 are kept locally.

A song’s menu offers **Adjust volume…** to trim that one song by up to 12 dB. The trim is kept for that song, applies whether or not volume normalization is on, and appears in **Track details**.

**Settings → Playback → Fade out before sleep** lowers the audio over the last 30 seconds of a timed or end-of-track sleep timer. Your chosen volume stays saved and is restored when the timer ends or is cancelled.

Drag a queue row sideways to remove it; the row lifts into its own color while you carry it, and the gap it will fall into is drawn as you drag it up or down. Undo restores it.

Queue headings distinguish songs added manually, collection tracks and autoplay recommendations when their origin is known. These labels preserve playback order, including after dragging songs. Older queues without origin information retain source headings.

Hold **Shift while dragging the seek bar** for fine seeking; the new position applies when you release. **⇧← / →** seeks by 100ms. Escape cancels a fine drag. The mouse wheel over the seek bar moves playback in five-second steps. **0**–**9** jump to that tenth of the track, with the same on-screen feedback as the other seek shortcuts; they are ignored while you are typing or while a song list has the keyboard.

Click the volume icon for a slider and an exact percentage. Enter a value from 0 to 100 and press Enter or Apply. This works in the main, mini and immersive players. In the main and immersive players, **⌘↑ / ↓** adjusts volume and **M** toggles mute; shortcuts show brief playback feedback.

Timed lyrics show a countdown during intros and explicit gaps of at least five seconds. Musix uses supplied line boundaries or blank timed lines; it does not infer instrumental passages from a long lyric line. Timing adjustments apply to the countdown.

The queue shows remaining time and a finish estimate during uninterrupted playback. Unknown durations, random shuffle, repeat, autoplay or a sleep timer can make a finish estimate unavailable.

Album pages show the artist, release year when available, track count and duration, with disc headings when the source supplies disc numbers. Drag the lyrics/queue divider to resize the panel; double-click it to reset. Its width is remembered.

Press **⌘⇧P** for quick actions, saved playlists and audio outputs. Type to filter, use the arrow keys, then press Enter.

**Listening sessions** in Settings or Quick Actions save your queue, song position, speed, shuffle, repeat and autoplay settings. Resume asks before replacing the current queue. Sessions can be renamed, updated or deleted; up to 20 sessions of 2,000 songs each are kept locally.

The arrow beside the player’s volume controls opens an audio-output picker. It remains available in narrow windows.

**Settings → Playback → Volume normalization** evens out loudness between recordings. ReplayGain and R128 tags are read when a file is imported; everything else, including YouTube and server audio, is measured from the decoded stream and levelled from the next play onward. Recordings shorter than 45 seconds of playback are never treated as measured. Gain is limited to -15 dB and +6 dB, and the mixer cannot amplify past full scale, so a quiet track is only lifted while your own volume leaves headroom. Your chosen volume is never rewritten, and **Track details** shows the correction in use. Up to 2,000 measurements are kept locally. Existing imports need a rescan to pick up their tags.

**Track details** shows playback codec, bitrate and decoded sample rate/channels when reported by the decoder. Local file metadata is labeled separately. Missing values are omitted.

**Settings** groups controls into Appearance, Playback, Library, Connections, and Privacy & data. Search finds controls across all categories. Narrow windows use a category selector.

Open a song’s menu to queue it, like it or add it to a playlist. Local playlist additions skip duplicates and can be undone. Views remember their filter, sort and scroll position during the session. Open **Clean up** in a local playlist to review duplicates and missing files; removal never deletes the original audio.

## Connect a music server

Open **Settings → Connections → Music server** and choose **Subsonic** (including Navidrome) or **Jellyfin**, and enter your server address, username and password. Use the server root, including any deployment subpath, without `/rest` or `/web`. Use HTTPS for remote servers.

Open **Library → Music server** to browse. The main search bar searches your server while this view is open. The server menu offers library selection and playlist creation. Permitted playlists support renaming, song removal and drag reordering; deletion requires owner or administrator permissions. Subsonic also offers ratings and server queue save/restore. Jellyfin shared playlists respect the server’s editing permissions.

Local playlists can mix YouTube, local files and server songs. Server playlists accept songs from that server only. One server account can be connected at a time. Server lyrics use synchronized lyrics when available, otherwise plain text.

**Remember in desktop keyring** needs a freedesktop Secret Service, which macOS does not provide, so on macOS a server connection lasts for the current session and the app says so when you connect. The address and username are remembered; the password, and Jellyfin's session token, are not. Passwords and authenticated URLs are never written into library exports. Disconnect clears the saved address and username.

Connection settings include audio quality and server listening history. Original audio is buffered on disk before playback, with a 512 MiB limit per song; choose a lower bitrate for very large files. Server transcoding must be available for the selected bitrate. Subsonic listening history is submitted after half a song or four minutes of playback, whichever comes first. Jellyfin receives playback status and progress and manages its own play counts. Private listening disables these reports.

Tested against Navidrome 0.63.2 and Jellyfin 10.11.11 / 12.0. Other servers must support Subsonic 1.16.1 token authentication and JSON responses. OpenSubsonic lyrics and form POST are detected when available. Jellyfin 10.11 removes duplicate playlist additions on the server; 12.0 preserves them. Jellyfin collections are paginated; a single opened collection is limited to 20,000 items. Server administration, video, podcasts, remote-device control and permanent offline downloads are outside this music integration.

## Accounts and saved data

**Settings → Connections → YouTube → Streaming quality** chooses what a song costs to download. Standard takes the best stream YouTube offers, which is Opus at about 130 kbps. Data saver caps it, which lands on Opus at about 67 kbps and a little over half the bytes. Musix buffers the whole song before playing it, so this is the download either way. No account or sign-in is involved, and neither setting is a lossless one; for lossless audio use local files or a music server set to Original.

YouTube browsing is anonymous. YouTube likes, local playlists and local history are stored locally and **do not sync with your Google account**. Settings offers library JSON import/export; audio files, custom cover images and imported LRC files are not bundled into exports.

For streams requiring sign-in, Settings can import a user-selected Netscape-format cookie file. Musix does not read your browser profile. Cookies can be removed in Settings.

Library data is stored in `~/Library/Application Support/Sung/sung/`, settings in `~/Library/Preferences/com.sung.sung.plist`, and cache in `~/Library/Caches/Sung/sung/`. Musix has no analytics or telemetry. Optional LRCLIB lyric lookups send the song’s title, artist and duration; they can be disabled in Settings.

## Troubleshooting

Playback depends on YouTube availability, region and network conditions. Musix buffers audio before playing, so starting a song can take a moment. It does not remove sponsor segments embedded in recordings or promise gapless playback.

YouTube occasionally changes how audio is served, and the resolver has to catch up. The macOS app does this itself: it refreshes `yt-dlp` and `ytmusicapi` in the background at most once a day, and again straight away if playback fails, keeping the previous working versions if an update turns out to be broken. There is nothing to run.

To update Musix, download the newer DMG and drag it over the old app; your library and settings are kept. To remove it, drag **Musix** from Applications to the Bin. Its data stays in `~/Library/Application Support/Sung/` until you delete that too.

