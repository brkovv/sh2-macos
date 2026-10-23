# Uninstall / Удаление

Close the game. Open ~/Applications/Uninstall Silent Hill 2.command, or open
Uninstall-SH2.command from a downloaded source-installer ZIP.

The tool displays the exact app/data paths and offers:

1. Remove the app, shortcuts, imported game copy, source/build caches, isolated
   Python environment, logs and setup downloads, while exporting saved progress,
   key bindings, configuration and prior save/config backups into:
   ~/Library/Application Support/SH2-brkovv-Saves/<timestamp>/
2. Remove the whole owned installation including saves/backups and prior save
   exports made by this tool. This requires typing **DELETE SH2**.

Press Enter without selecting a mode to cancel. For explicit terminal automation,
--yes retains saved progress; --yes --remove-saves selects complete removal.

The original PC game directory is never an uninstall target. Unknown files or
edited shortcuts are preserved and reported. Shared Homebrew, its formulae and
Apple Command Line Tools remain installed because other software can use them;
the project's own Python environment and caches are removed.

The tool refuses broad/system/home paths, root symlinks, unrecognized ownership
markers and deletion while the game/setup holds its lock. Internal source/game
links are removed without traversing their targets. These paths are tested with
isolated Linux fixtures; actual macOS removal is not yet verified.
