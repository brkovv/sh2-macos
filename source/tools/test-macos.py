"""Interactive first Mac run: verify inputs, snapshot mutable lab state, collect diagnostics."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

EXPECTED_EXE = 'f31a221779fadca4dae8fe758546ecde8b88a88fa6e80fad7dee115d2ef620e8'


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='Writable EE game folder containing sh2pc.exe, data and sh2e')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--logs-dir', type=Path, help='Diagnostic folder (default: project logs)')
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        parser.error('Run from a native arm64 terminal on an Apple Silicon Mac.')
    root = Path(__file__).resolve().parents[1]
    binary = (args.binary or root/'build/macos-arm64/sh2').resolve()
    game = args.game.resolve(strict=True)
    exe = game/'sh2pc.exe'
    if not binary.is_file():
        parser.error('Mac executable missing: run tools/build-macos.sh first.')
    with binary.open('rb') as stream:
        header = stream.read(8)
    if header != bytes.fromhex('cffaedfe0c000001'):
        parser.error('Expected a thin ARM64 Mach-O executable, not a Linux ELF or Windows EXE.')
    if not exe.is_file() or sha256(exe) != EXPECTED_EXE:
        parser.error('Unsupported/missing sh2pc.exe. Use your supported Enhanced Edition installation.')
    if not all((game/name).is_dir() for name in ('data', 'sh2e')):
        parser.error('Game folder must contain data/ and sh2e/ resources.')
    stamp = datetime.now().astimezone().strftime('%Y%m%d-%H%M%S-%f')
    evidence = (args.logs_dir or root.parent/'logs').expanduser().resolve()/f'macos-run-{stamp}'
    before = evidence/'before'
    before.mkdir(parents=True)
    for item in game.iterdir():
        if item.is_file() and item.suffix.lower() in {'.ini', '.dat', '.fix', '.bin', '.log'}:
            shutil.copy2(item, before/item.name)
    if (game/'data/save').is_dir():
        shutil.copytree(game/'data/save', before/'data-save')
    info = []
    for command in (['sw_vers'], ['uname', '-a'], ['sysctl', '-n', 'hw.memsize'],
                    ['file', str(binary)], ['otool', '-L', str(binary)]):
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        info.append('$ '+' '.join(command)+'\n'+result.stdout+result.stderr)
    (evidence/'machine.txt').write_text('\n'.join(info), encoding='utf-8')
    env = os.environ.copy()
    # This run uses real input and native Mac audio, independent of the Windows/WSL harness.
    for key in ('SH2_KEYS', 'SH2_SHOW', 'SH2_LAUNCHER', 'SH2_WAV', 'SH2_SHOT', 'SH2_DUMP_AFTER',
                'SH2_RELAY_PCM', 'SH2_AUDIO_DECLICK', 'SH2_TEST_BINARY', 'SDL_AUDIODRIVER',
                'GALLIUM_DRIVER', 'MESA_GL_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE'):
        env.pop(key, None)
    env.update(SH2_STATS='1', SH2_AUDIO_STATS='1', SH2_INPUT_STATS='1')
    print(f'Backup and logs: {evidence}', flush=True)
    print('Launching native game. Close its window to finish the test.', flush=True)
    started = time.time()
    with (evidence/'console.log').open('wb') as log:
        proc = subprocess.Popen([str(binary), str(game)], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = proc.wait()
        except KeyboardInterrupt:
            if proc.poll() is None:
                proc.terminate()
            try:
                code = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                code = proc.wait()
    guest_logs = {}
    for name in ('sh2.log', 'sh2-crash.log'):
        path = game/name
        if path.is_file():
            shutil.copy2(path, evidence/name)
            guest_logs[name] = {'modified_during_run': path.stat().st_mtime >= started}
    report = dict(platform=platform.platform(), architecture=platform.machine(),
                  binary=str(binary), binary_sha256=sha256(binary), game=str(game),
                  exe_sha256=EXPECTED_EXE, pid=proc.pid, returncode=code,
                  seconds=round(time.time()-started, 2), guest_logs=guest_logs,
                  native_audio_audibility_verified=False,
                  note='User observation is required for visual/audio behavior; exit alone does not verify gameplay.')
    (evidence/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Game exited with code {code}. Diagnostics: {evidence}', flush=True)
    return 0 if code == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
