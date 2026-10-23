# 0.1.0-alpha.4 — managed installer, updater and uninstaller

- Install-SH2.command installs a persistent app and Start/Update/Uninstall commands
  into ~/Applications. The data path is automatic, or selectable once with --base.
- Missing Apple/Homebrew/dependency setup is started automatically. Apple/vendor
  authentication and confirmation dialogs still require the user.
- Explicit GitHub updater verifies repository, size, SHA-256, ZIP paths and file
  checksums before staging; replacement failure restores the previous app.
- Uninstaller offers keep-progress or confirmed complete removal, including old
  save exports. Shared system tools and the original PC game are retained.
- Visible build output, --no-launch, and source-content cache reuse across updates.
- Runtime/game source files are unchanged from alpha.3. No game assets included.

Real lifecycle implementation is tested on Linux with private temporary fixtures.
Actual Apple setup, LaunchServices, clean Mac build/install/uninstall/update and
full-game stability remain unverified. This is an unsigned source-installer alpha.
# 0.1.0-alpha.3 вЂ” first public alpha

- Native Apple Silicon source installer using the player's own supported EE game.
- Desktop display/movie options, mouse input and optional Russian text.
- Experimental 60 FPS timing corrections and native ADX music path.
- Separate writable game copy, configuration/save backups and local build cache.
- Installation, build, troubleshooting and contribution documentation.
- Original sh2-nx author credited; permission received; MIT scope and component
  notices preserved. Private correspondence and developer handoff are excluded.

The runtime sources are byte-identical to the cleanly rebuilt alpha.1/alpha.2
candidates. Clean Linux generation/build/early execution and installer helper
tests passed. The clean source-installer route on a Mac, extended Apple audio,
later scenes and full save/load coverage remain unverified. Unsigned source
installer; no prebuilt game, tested app or DMG is supplied. This is a prerelease.
