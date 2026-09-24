# What an Apple Developer ID would unlock

Musix is ad-hoc signed (`codesign -s -`). That gives it a valid signature
but no identity: nothing ties one build to the next, and Apple has never seen
it. A Developer ID comes with the Apple Developer Program ($99 a year) and
changes that.

## Direct wins

- **Notarization, so no Gatekeeper warning.** Apple scans a notarized build
  and the DMG carries a stapled ticket. The first launch opens normally,
  without a trip to Privacy & Security.
- **No Open Anyway on every update.** Today approval covers one exact build,
  so each release has to be approved again. A notarized build needs no
  approval at all.
- **Keychain password storage.** A Keychain item's access list names the
  app by its signature. An ad-hoc signature changes with every build, so
  macOS would ask for the Keychain password after each update, which is why
  "Remember" is hidden on macOS. With a Team ID the requirement stays the
  same across releases, and the modern data-protection keychain (which
  needs a `keychain-access-groups` entitlement and so a Team ID) avoids the
  prompts entirely. That makes a macOS "Remember password" for Music server
  and ListenBrainz tokens straightforward.

## Also likely

- **Privacy permissions that survive updates.** macOS remembers some
  permissions (for example access to Desktop, Documents or Downloads) by the
  app's code requirement. For an ad-hoc app that requirement is the exact
  build, so an update can ask again. A Developer ID keeps it stable. Not yet
  tested on Musix.
- **An in-app updater.** Musix only tells people a release exists; it never
  installs one. An updater such as Sparkle could install updates itself,
  but it only makes sense once the new build opens without a Gatekeeper
  stop.
- **Homebrew.** Homebrew has announced it will drop casks whose apps fail
  Gatekeeper, so a `brew install --cask musix` needs a signed, notarized
  build.
- **MusicKit.** Apple Music catalogue access through MusicKit needs a
  developer token, which needs the program. Musix's Apple Music artwork
  lookup doesn't use MusicKit today and doesn't need it.

## Not needed for

The Mac App Store is a separate route with its own certificate, review and
sandbox, and isn't planned. Media keys, Now Playing, notifications and the
window chrome all work without a Developer ID.
