# Installation / РЈСЃС‚Р°РЅРѕРІРєР°

This is a experimental source-installer alpha, not a stable prebuilt game.

Extract the Source-Installer ZIP and open Start-SH2.command. It runs in
Terminal and requires macOS 15+, Apple Silicon, Apple Command Line Tools and
Homebrew. The installer installs the remaining build dependencies. No prebuilt
game or tested binary app/DMG is included in this alpha.

Select your PC/Enhanced Edition game folder. The complete supported executable
is checked before import. Your own data is copied to a separate writable location;
the original installation is left alone. Source generation and compilation happen
locally on the first run. An existing translated installation is imported too.

If you previously used the private Mac kit, its game folder can be selected:
~/Library/Application Support/SH2-Recomp/2b7a75bcb3a601b9/game

Runtime data, source cache, logs and backups are separate from the app:
~/Library/Application Support/SH2-brkovv/

Moving the app or mounting a read-only DMG must not move or erase saves. Keep
game/data/save and backups when reinstalling. To remove the app, remove the app
bundle; the separate game folder remains until you deliberately remove it.

Actual app launch on macOS, signing and notarization are not verified here.
