# Install / Установка

Download the Source-Installer ZIP from this repository's Releases, extract it,
then open Install-SH2.command. Start-SH2.command is also a setup/launch entry point.
Requires Apple Silicon, macOS 15+, internet and your own supported PC/EE game.

The installer places its app in ~/Applications/Silent Hill 2.app and creates Start,
Update and Uninstall Silent Hill 2.command shortcuts in ~/Applications. You can
remove the extracted ZIP folder once setup completes. Sources, game copy, builds,
logs and isolated Python environment live separately under:

~/Library/Application Support/SH2-brkovv/

It starts missing Apple Command Line Tools setup, waits up to 20 minutes for the
SDK, and runs a pinned, SHA-256-verified official Homebrew installer if necessary.
Apple/Homebrew may require a password, confirmation or completing a system dialog.
It installs cmake, ninja, pkgconf, Python 3.12, SDL2, FFmpeg and isolated Capstone.
It never disables macOS security protections. On interruption, rerun the installer.

Select your own PC/EE game folder once; the executable hash and required resources
are checked before import. If the old SH2-Recomp private-kit game folder exists,
it is selected automatically. The original is copied, not modified. An existing
alpha.3 default data folder is reused after validation. Settings/saves are backed
up before defaults/build. Existing explicit settings remain in effect.

For a custom **data** directory on first install, use Terminal:

```sh
bash /path/to/Install-SH2.command --base "$HOME/Games/SH2-data"
```

The directory must be dedicated to SH2, empty or already owned by this installer.
Do not select the original game folder. The selected path is remembered. Moving
an existing installation between data paths is not automated in this alpha.

Generation/build progress is visible; details are in the data folder's logs.
Use --no-launch to install/build without starting the game. After an error the
bootstrap log is printed; after initialization it is also copied into logs.

**Experimental:** actual clean Mac installation, app/LaunchServices, Homebrew/Apple
dialogs and game execution through this new manager have not been tested on a Mac.
Linux lifecycle tests are reported separately; they do not establish Apple support.
