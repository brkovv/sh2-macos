<div align="center">

# Silent Hill 2 · macOS

**An experimental native Apple Silicon adaptation by brkovv**

Built on [cutarev/sh2-nx](https://github.com/cutarev/sh2-nx) and its embedded xboxrecomp.

[Download alpha](https://github.com/brkovv/sh2-macos/releases) ·
[Installation](docs/INSTALLATION.md) · [Building](docs/BUILDING.md) ·
[Report a bug](https://github.com/brkovv/sh2-macos/issues) · [Telegram](https://t.me/brkovv)

**0.1.0-alpha.4 · Native ARM64 · Source installer · MIT**

</div>

Silent Hill 2 through a native compatibility layer and static x86-to-C
recompilation. The project keeps the original fixed-camera gameplay and adds
Mac display, input and installation support.

**Bring your own game.** No game executable, generated game C, textures, music,
movies or other game resources are included. This is an unofficial fan project.

> **Experimental alpha:** the source installer has passed clean Linux generation,
> build and early-game tests. A clean installation from this public source package
> on macOS has not yet been verified. Earlier Mac testing used a private development
> kit. Full-game stability, Apple audio over long sessions and save compatibility
> across the game remain unverified. This is not a stable release.

## Features

- Native Apple Silicon build route; no Wine in the supplied installer.
- 16:10, 16:9 and 4:3 display options, including movie scaling options.
- Experimental 60 FPS timing corrections; 30 FPS remains selectable.
- Native ADX music playback and audio/thread timing fixes.
- Mouse navigation in the launcher and mouse controls during gameplay.
- Keyboard/gamepad support and preservation of existing key mappings.
- Optional Russian text through the credited Metallist/Hellbringer/ilyuXer pack.
- Title art and fonts loaded from your own installation.
- Automatic installation into ~/Applications with separate writable game data.
- One-command GitHub updates with archive verification and app replacement rollback.
- Uninstaller with keep-saves and complete-removal modes.
- Shared source/build cache and visible generation/build progress.

## Requirements

| Component | Required |
| --- | --- |
| Mac | Apple Silicon / ARM64; macOS 15 or newer, experimental target |
| Apple tools | Command Line Tools (`xcode-select --install`) |
| Dependencies | Missing [Homebrew](https://brew.sh) and libraries are installed automatically; Apple/vendor prompts may require confirmation |
| Game | Your own PC Silent Hill 2 installation with Enhanced Edition resources |
| Executable | Supported sh2pc.exe SHA-256 below |
| Storage/network | Space for a separate game copy and build cache; internet for dependencies |

```text
f31a221779fadca4dae8fe758546ecde8b88a88fa6e80fad7dee115d2ef620e8
```

Intel Macs and older macOS versions are not supported by this installer.
The hash is checked before importing the game. An arbitrary Director's Cut
executable is not interchangeable with the supported version.

## Install / Установка

1. Download **SH2-macOS-0.1.0-alpha.4-Source-Installer.zip** from
   [Releases](https://github.com/brkovv/sh2-macos/releases).
2. Extract it and open **Install-SH2.command**. The installer chooses the standard
   paths and starts missing Apple/Homebrew setup. Complete any system prompts.
3. Select your PC/Enhanced Edition folder. The installer copies it to a separate
   writable location, installs dependencies, generates code and builds.
4. When **«Сборка завершена. Запускаем игру.»** appears, the game starts.

Первый запуск собирает игру из вашей копии — это может занять время.
Архив не содержит готовую игру. Повторные запуски используют локальный кэш.
После установки запускайте ~/Applications/Silent Hill 2.app или Start Silent Hill 2.command.
Подробности: [Установка](docs/INSTALLATION.md) и [решение проблем](docs/TROUBLESHOOTING.md).

The app is installed into `~/Applications/Silent Hill 2.app`. Start, Update and
Uninstall command shortcuts are installed alongside it. Game files, saves, backups,
build cache and logs live under:

```text
~/Library/Application Support/SH2-brkovv/
```

The original PC folder is preserved. A custom data path is supported with
`Install-SH2.command --base /absolute/path/to/SH2-data`; the path is remembered.
See [updates](docs/UPDATING.md) and [uninstallation](docs/UNINSTALLING.md).

## Controls

| Input | Action |
| --- | --- |
| Mouse movement | Turn James |
| Left mouse button | Action / attack |
| Right mouse button | Ready weapon |
| Hold middle mouse + move | Original search camera |
| F8 (Fn+F8 on some Macs) | Toggle gameplay mouse mode |

Keyboard/gamepad bindings remain configurable. Existing bindings are imported;
consult the in-game controls menu for your layout. The camera remains the
original fixed/search camera. Cinematics use their scripted camera.

## Russian text / Русский текст

Existing translations/russian files are imported with the game. To fetch the
pinned optional text pack, run:

```sh
bash /path/to/Start-SH2.command --russian
```

The installer verifies the archive and files. Select Russian in the launcher
and restart. Voices remain original. Changing only the original game's language
setting does not reload an already loaded font.

Translation: **Metallist**, adaptation **Hellbringer**, technical fixes **ilyuXer**.
[Translation source](https://github.com/ilyuXer/Silent-Hill-2-Enchanced-Edition-Rus).

## Development and verification

The runtime base is pinned to sh2-nx
95ae2f16efc09c82f405f04383a27d98c2fa804a. Sources are in source/.
Generated game code stays on the user's machine and is excluded from Git.

- [Build and optional app/DMG packaging](docs/BUILDING.md)
- [Validation and limitations](VALIDATION.json)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](RELEASE_NOTES.md)

```sh
python3 release-check.py
python3 release-check.py --publish-check --prerelease
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_installation.py
```

Later scenes, puzzles, combat, save/load and extended Mac audio need more testing.
The optional app layout and DMG builder are unsigned and not notarized; actual
Apple launch and DMG creation remain unverified here. No binary app or DMG
is advertised as a tested release asset.

## Authors and license

**cutarev** created the original [sh2-nx](https://github.com/cutarev/sh2-nx).
**brkovv** maintains this macOS adaptation, published with upstream permission
and credit to sh2-nx.

Port contributions covered by that permission and brkovv's additions use
[MIT](LICENSE). Third-party material keeps its own terms; see
[license scope](LICENSE_STATUS.md) and [full credits](CREDITS.md).
Silent Hill 2 belongs to KONAMI / Team Silent. This project is not endorsed by them.

Engineering, debugging and packaging were assisted by **Codex**. The launcher
loads artwork from the user's game; no generated artwork is included.
