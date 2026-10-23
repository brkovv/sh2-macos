# Building and platform packaging

The source package pins the supported EE executable hash, Capstone 5.0.7 and the
sh2-nx commit in SOURCE-MANIFEST.json. The embedded xboxrecomp tree is the pinned
port's toolchain. Original game assets and generated C are never source payloads.

Start-SH2.command creates an isolated Python environment, imports the user's own
game and drives tools/regen.sh, then tools/build-macos.sh. Keep -fwrapv and
-fno-strict-aliasing. Existing configuration values and saves are retained.

To assemble an installer app and DMG on an actual Mac, run Package-Mac.command.
It puts the output in a new Desktop folder and refuses to overwrite existing
output. Alternatively:

```sh
python3 build-dmg.py --kit /path/to/extracted-kit --output /path/outside-kit/installer
```

Only checksummed kit files are copied into the app. Its shell bootstrap opens
Start-SH2.command in Terminal. It contains source/tools, not a game executable.
hdiutil creates a compressed DMG and verifies that image. This is not code signing
or notarization, nor proof that the game runs on an Apple GPU/audio device.

On Linux/Windows, --bundle-only can verify app metadata, relative paths and payload
checksums. It cannot produce a real DMG or verify Apple LaunchServices.

Fresh generation, native Linux linking and early-game execution have passed.
Actual clean Mac installation and broader game coverage remain pending. See
RELEASE_STATUS.json and VALIDATION.json before describing this as released.
