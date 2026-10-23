# Silent Hill 2 — brkovv macOS adaptation

## Project authors

- **cutarev** — creator of the original **sh2-nx** port and its Windows compatibility layer:
  https://github.com/cutarev/sh2-nx .
- **brkovv** — macOS adaptation and launcher identity; Telegram **tg@brkovv**:
  https://t.me/brkovv .

## Foundations and contributors

The macOS work builds upon **cutarev/sh2-nx**, pinned reference commit
`95ae2f16efc09c82f405f04383a27d98c2fa804a`: https://github.com/cutarev/sh2-nx .
The original project's credits also acknowledge **r4lix and Le Z** for Enhanced
Edition support, and **sp00nznet** for xboxrecomp. Their original notices and
the upstream README remain in the source payload.

Its embedded **xboxrecomp** supplies the static recompilation foundation;
retain its NOTICE/LICENSE files and component notices in any permitted release.

**Silent Hill 2: Enhanced Edition** supplies the enhancement designs and supported
resource formats: https://github.com/elishacloud/Silent-Hill-2-Enhancements .
Adapted portions keep their original notices. This is not an official EE release.
The original **Silent Hill 2** game belongs to **KONAMI / Team Silent**.

Russian text: **Metallist**, adaptation **Hellbringer**, technical fixes **ilyuXer**:
https://github.com/ilyuXer/Silent-Hill-2-Enchanced-Edition-Rus,
pinned commit `0948fe2b637d009711834a1096e5f1154badc422`.
Translation files are downloaded from that source at installation time.

Libraries used by the existing port include SDL2 and FFmpeg. Preserve the exact
component licenses for the versions used in a future permitted distribution.

The classic launcher reads the title artwork and menu font from the user's
installed game / Enhanced Edition / Russian overlay. These game assets are
not included in the update. **DejaVu Serif** remains the fallback for missing
files/glyphs; see `assets/FONT-LICENSE.txt` in the source payload.

AI disclosure: adaptation, debugging, packaging and launcher implementation were
assisted by **Codex**. The earlier brkovv-v1 launcher used a fog-road background
generated with **image_gen** from text alone. The classic launcher no longer
embeds or displays that image. Local game assets were never uploaded.

This macOS adaptation is published with cutarev's permission and proper credit
to the original sh2-nx. See LICENSE and LICENSE_STATUS.md for the scope of MIT
and the preserved component terms. Game resources are not included.
