# Contributing

Please open an issue describing the problem or proposed change before a large PR.
Include your Mac model/chip, macOS version, release version, scene and steps to
reproduce. Test at both 30 and 60 FPS when investigating timing problems.

Do not commit game files, generated game C, executable builds, saves, personal
logs or credentials. Share only the relevant log excerpt after checking it for
private paths or identifiers. Keep all existing attribution and license notices.

The source installer and runtime are in this repository; game assets are supplied
locally. See docs/BUILDING.md. Keep -fwrapv and -fno-strict-aliasing. Modify the
adapter/lifter source rather than hand-editing generated C. Describe the actual
host and tests run; Linux success is not Apple GPU/audio evidence.
