#!/usr/bin/env python3
"""Writes assets/font.png, the launcher's font (src/launcher.c): ASCII 32-126 in a 16x6 grid of 40x80 cells,
white on transparent. Needs ImageMagick and Source Code Pro (SIL OFL)."""
import subprocess, sys
font = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Medium.otf'
cmd = ['magick', '-size', '640x480', 'xc:none', '-font', font, '-pointsize', '60', '-fill', 'white']
for i in range(95):
    c = chr(32 + i)
    text = {'%': '%%', '\\': '\\\\', '@': '\\@'}.get(c, c)
    cmd += ['-annotate', f'+{i % 16 * 40 + 2}+{i // 16 * 80 + 60}', text]  # baseline 60 px into the cell
subprocess.run(cmd + ['PNG32:assets/font.png'], check=True)
