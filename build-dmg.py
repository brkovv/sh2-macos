"""Assemble a source-installer app; create its DMG only on a real Mac.

--bundle-only validates a portable app layout, not Apple execution/signing.
Only files in the kit's checksum manifest are copied. No game binary is added.
"""
import argparse,hashlib,json,os,plistlib,shutil,subprocess,sys
from pathlib import Path,PurePosixPath

def kit_files(kit):
    manifest=kit/'FILES.sha256';found={}
    for line in manifest.read_text(encoding='utf-8').splitlines():
        digest,name=line.split('  ',1);path=PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or path.as_posix()!=name or name in found:
            raise RuntimeError('Invalid kit manifest path: '+name)
        if len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):raise RuntimeError('Invalid checksum: '+name)
        file=kit.joinpath(*path.parts)
        if file.resolve()!=file or not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Changed kit file: '+name)
        found[name]=file
    for required in ('Start-SH2.command','installer.py','RELEASE_STATUS.json','SOURCE-MANIFEST.json'):
        if required not in found:raise RuntimeError('Missing kit entry: '+required)
    found['FILES.sha256']=manifest
    return found

def assemble(kit,output):
    kit=kit.resolve(strict=True);output=output.resolve()
    if output==kit or kit in output.parents:raise RuntimeError('Output must be outside the source kit.')
    if output.exists():raise RuntimeError('Output already exists; it was preserved: '+str(output))
    files=kit_files(kit)
    status=json.loads((kit/'RELEASE_STATUS.json').read_text());version=status['version']
    output.mkdir(parents=True)
    app=output/'Silent Hill 2.app';contents=app/'Contents';resources=contents/'Resources/kit'
    resources.mkdir(parents=True);(contents/'MacOS').mkdir()
    for name,source in files.items():
        dest=resources/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
        dest.chmod(0o755 if name.endswith(('.command','.sh')) else 0o644)
    launch=contents/'MacOS/SH2'
    launch.write_text('''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../Resources/kit" && pwd)
exec /usr/bin/open -a Terminal "$HERE/Start-SH2.command"
''',encoding='utf-8',newline='\n');launch.chmod(0o755)
    numeric=version.split('-')[0]
    info=dict(CFBundleIdentifier='io.github.brkovv.sh2-macos.installer',CFBundleName='Silent Hill 2',
        CFBundleDisplayName='Silent Hill 2',CFBundleExecutable='SH2',CFBundlePackageType='APPL',
        CFBundleShortVersionString=numeric,CFBundleVersion='2',LSMinimumSystemVersion='15.0',
        LSArchitecturePriority=['arm64'],NSHighResolutionCapable=True)
    (contents/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True))
    (contents/'PkgInfo').write_bytes(b'APPL????')
    # Standard Finder shortcut. A ZIP uses a regular text instruction instead.
    if sys.platform=='darwin':(output/'Applications').symlink_to('/Applications',target_is_directory=True)
    (output/'INSTALL-RU.txt').write_text('''Перетащите Silent Hill 2.app в Программы и откройте.
Приложение откроет установщик в Terminal. Выберите свою папку PC-игры с Enhanced Edition.
Первый запуск копирует ваши файлы в отдельную папку и собирает порт из исходников.
В этом комплекте нет готовой игры, её бинарника или ресурсов.
Это приватный тестовый кандидат. Подпись/нотаризация и запуск на Mac ещё не подтверждены.
''',encoding='utf-8')
    return app,version,len(files)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kit',type=Path,default=Path(__file__).resolve().parent)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--bundle-only',action='store_true')
    args=parser.parse_args()
    if not args.bundle_only and sys.platform!='darwin':parser.error('DMG creation requires macOS/hdiutil. --bundle-only only validates layout.')
    if not args.bundle_only and not shutil.which('hdiutil'):parser.error('hdiutil was not found.')
    app,version,count=assemble(args.kit,args.output)
    report=dict(app=str(app),version=version,files=count,game_binary_included=False,
        source_installer=True,signed=False,notarized=False,actual_apple_launch_verified=False)
    if not args.bundle_only:
        dmg=args.output.parent/('SH2-macOS-'+version+'-UNPUBLISHED.dmg')
        if dmg.exists():raise RuntimeError('Existing DMG preserved: '+str(dmg))
        subprocess.run(['hdiutil','create','-volname','Silent Hill 2','-srcfolder',str(args.output),
                        '-format','UDZO',str(dmg)],check=True)
        subprocess.run(['hdiutil','verify',str(dmg)],check=True)
        report.update(dmg=str(dmg),dmg_sha256=hashlib.sha256(dmg.read_bytes()).hexdigest(),dmg_verified=True)
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    try:main()
    except Exception as error:print(str(error),file=sys.stderr);raise SystemExit(2)
