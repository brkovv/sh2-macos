# Updates

Close the game, then open ~/Applications/Update Silent Hill 2.command. It checks
only public releases in brkovv/sh2-macos. Alpha installations include newer alpha
releases; a stable installation does not automatically opt into prereleases.

The archive must have GitHub's SHA-256 digest and the expected repository/asset
address. Download size, version, paths, links and every packaged file are checked.
The new app is staged separately; replacement/registration failure restores the
previous app. A running game/setup locks the data folder and blocks updates.
TLS validation stays enabled. Checksums detect corruption; releases are unsigned.

Game data, saves and explicit settings remain in the same data folder. The next
launch builds as needed. Packaging-only updates reuse a content-based source cache;
the first move from alpha.3 to alpha.4 uses a new cache layout and may rebuild once.
Old caches are retained until uninstallation. No game archive is downloaded.

**From alpha.3:** download alpha.4 once and open Install-SH2.command. Its updater
becomes available after that installation. You can also install a downloaded newer
Source-Installer ZIP manually over an existing owned installation.

No background monitor or automatic upload is used. Actual network/update/app launch
on macOS remains unverified; private fixture tests cover success and failure paths.
