"""Derive desktop hook translation units in the build tree; never edit input generated C.

Only exact, fingerprinted v10-ee functions are adapted. Original generation remains
the source of truth, and a mismatched regeneration fails before compiling.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

def adapt(source, destination, manifest):
    outputs = {}
    for entry in manifest:
        unit = entry['unit']
        text = outputs.get(unit)
        if text is None:
            text = (source/unit).read_text()
        name = entry['function']
        pattern = r'^void '+name+r'\(void\)\n\{.*?^\}'
        match = re.search(pattern, text, re.M|re.S)
        if not match and entry.get('regenerated_manual_sha256'):
            # A fresh --exclude-manual run omits a complete hand-written override.
            # The old private generation still contains its unused original body.
            manual = Path(__file__).resolve().parents[1]/'src/game/ee.c'
            override = re.search(r'^void '+name+r'\(void\)\s*\{.*?^\}',manual.read_text(),re.M|re.S)
            dispatch = '    { 0x'+name[4:]+'u, (recomp_func_t)'+name+' },'
            duplicate = any(re.search(pattern,p.read_text(),re.M|re.S) for p in source.glob('recomp_*.c'))
            if (not override or hashlib.sha256(override[0].encode()).hexdigest()!=entry['regenerated_manual_sha256']
                or dispatch not in (source/'recomp_dispatch.c').read_text() or duplicate):
                raise ValueError('Unsupported regenerated manual override: '+name)
            continue
        accepted = [entry['sha256']]+entry.get('regenerated_sha256',[])
        if not match or hashlib.sha256(match[0].encode()).hexdigest() not in accepted:
            raise ValueError('Unsupported generated function: '+name)
        body = match[0]
        if entry.get('wrap'):
            body = body.replace('void '+name+'(void)', 'void '+name+'_desktop_original(void)', 1)
        for old, new in entry.get('replacements', []):
            if body.count(old) != 1:
                raise ValueError('Instruction anchor is not unique in '+name+': '+old[:80])
            body = body.replace(old, new, 1)
        outputs[unit] = text[:match.start()]+body+text[match.end():]
    destination.mkdir(parents=True, exist_ok=True)
    for unit, text in outputs.items():
        target = destination/unit
        header = Path(__file__).resolve().parents[1]/'src/game/desktop.h'
        text = '#include "'+header.as_posix()+'"\n'+text
        if not target.exists() or target.read_text() != text:
            target.write_text(text, newline='\n')
    return sorted(outputs)

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--manifest',type=Path,required=True)
    a=p.parse_args()
    print(';'.join(adapt(a.source,a.output,json.loads(a.manifest.read_text()))))
