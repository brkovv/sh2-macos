"""Install/update/remove only this project's owned app and data; stdlib only."""
import argparse
from contextlib import contextmanager
from datetime import datetime
import hashlib, json, os, plistlib, re, shlex, shutil, stat, sys, tempfile
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import zipfile

sys.dont_write_bytecode = True

APP_ID = 'io.github.brkovv.sh2-macos.installer'
REPO = 'brkovv/sh2-macos'
DATA_NAMES = ('game','versions','venv','logs','backups','russian-download-cache','updates','setup','runtime-status.json')
MAX_ARCHIVE = 20 * 1024**2

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024**2), b''): h.update(chunk)
    return h.hexdigest()

def canonical(path):
    path = Path(path).expanduser().absolute()
    if path.resolve() != path: raise RuntimeError('Ссылки и неоднозначные пути не поддерживаются: '+str(path))
    return path

def atomic(path, data):
    path = Path(path)
    canonical(path.parent)
    if path.is_symlink(): raise RuntimeError('Ссылка вместо файла: '+str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.sh2-write-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name): os.unlink(name)

def put_json(path, value):
    atomic(path, (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())

def read_owned(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file(): raise RuntimeError('Нет обычного маркера установки: '+str(path))
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('app_id') != APP_ID: raise RuntimeError('Это не установка SH2: '+str(path))
    return value

def registry(home): return canonical(canonical(home)/'Library/Application Support/SH2-brkovv-install.json')
def default_base(home): return canonical(canonical(home)/'Library/Application Support/SH2-brkovv')

def safe_base(base, home):
    home = canonical(home); base = canonical(base)
    protected = {home, home.parent, Path(base.anchor), home/'Library',home/'Library/Application Support',
        home/'Applications',home/'Desktop',home/'Documents',home/'Downloads'}
    if base in protected or base in home.parents:
        raise RuntimeError('Выберите отдельную папку SH2, не домашнюю или системную: '+str(base))
    app=home/'Applications/Silent Hill 2.app'
    if base==app or app in base.parents or base in app.parents:
        raise RuntimeError('Папки приложения и игровых данных должны быть раздельными.')
    return base

def location(home, requested=None):
    reg = registry(home)
    if requested is not None: return safe_base(requested,home)
    if reg.exists() or reg.is_symlink():
        value = read_owned(reg)
        return safe_base(value['base'],home)
    return default_base(home)

def verify_package(kit):
    kit = canonical(kit); found = {}
    for line in (kit/'FILES.sha256').read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ',1); p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name or p.as_posix()!=name:
            raise RuntimeError('Некорректный путь в комплекте: '+name)
        if not re.fullmatch('[0-9a-f]{64}',digest) or name.casefold() in {n.casefold() for n in found}:
            raise RuntimeError('Некорректный манифест комплекта.')
        target = kit.joinpath(*p.parts)
        if canonical(target)!=target or not target.is_file() or sha(target)!=digest:
            raise RuntimeError('Повреждён файл комплекта: '+name)
        found[name] = target
    actual = {p.relative_to(kit).as_posix() for p in kit.rglob('*') if p.is_file() or p.is_symlink()}
    # Finder may create this harmless metadata file after ZIP extraction.
    metadata = {p.relative_to(kit).as_posix() for p in kit.rglob('.DS_Store')
                if p.is_file() and not p.is_symlink() and p.stat().st_size<=65536}
    actual -= metadata
    if actual != set(found)|{'FILES.sha256'}: raise RuntimeError('Неизвестные файлы в комплекте.')
    for name in ('Start-SH2.command','Install-SH2.command','Uninstall-SH2.command','Update-SH2.command',
                 'manage-install.py','installer.py','VERSION','SOURCE-MANIFEST.json','LICENSE'):
        if name not in found: raise RuntimeError('Неполный комплект: '+name)
    found['FILES.sha256'] = kit/'FILES.sha256'
    return found

def init_base(base, home):
    base = safe_base(base,home); marker = base/'installation.json'
    if marker.exists() or marker.is_symlink():
        value = read_owned(marker)
        if value['base'] != str(base): raise RuntimeError('Маркер относится к другой папке.')
    else:
        if base.exists() and any(base.iterdir()):
            # Adopt only the known alpha.3 default installation, not arbitrary data.
            if base!=default_base(home): raise RuntimeError('Выбранная папка не пуста и не принадлежит SH2.')
            import importlib.util
            spec=importlib.util.spec_from_file_location('sh2_installer',Path(__file__).with_name('installer.py'))
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            canonical(base/'game')
            module.validate_game(base/'game')
        base.mkdir(parents=True,exist_ok=True)
        put_json(marker,dict(app_id=APP_ID,base=str(base)))
    return base

@contextmanager
def locked(base):
    import fcntl
    path = base/'.install.lock'
    if path.is_symlink(): raise RuntimeError('Ссылка вместо блокировки установки.')
    with path.open('a+') as f:
        try: fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: raise RuntimeError('Закройте игру или другой установщик и повторите.')
        f.seek(0); f.truncate(); f.write(str(os.getpid())); f.flush()
        yield

def remove_child(path, parent):
    path=Path(path); parent=canonical(parent)
    if path.parent!=parent or path.is_symlink() or canonical(path)!=path:
        raise RuntimeError('Удаление вне собственной папки запрещено: '+str(path))
    if path.is_dir(): shutil.rmtree(path)  # Internal links are unlinked, never followed.
    elif path.exists(): path.unlink()

def app_marker(app): return app/'Contents/Resources/installation.json'

def install_locked(kit, base, home):
    files=verify_package(kit); home=canonical(home)
    for name in DATA_NAMES:
        path=base/name
        if path.exists() or path.is_symlink():canonical(path)
    version=(kit/'VERSION').read_text().strip()
    if version_key(version) is None: raise RuntimeError('Некорректная версия комплекта.')
    apps=canonical(home/'Applications'); apps.mkdir(parents=True,exist_ok=True)
    app=apps/'Silent Hill 2.app'; reg=registry(home)
    if reg.exists() or reg.is_symlink():
        old_reg=read_owned(reg)
        if old_reg['base']!=str(base): raise RuntimeError('Уже используется другая папка данных; сначала удалите прежнюю установку.')
    if app.exists() or app.is_symlink():
        if app.is_symlink():raise RuntimeError('Ссылка вместо приложения.')
        if app_marker(app).is_file():
            value=read_owned(app_marker(app))
            if value['base']!=str(base):raise RuntimeError('Существующее приложение не принадлежит этой установке.')
        elif app==kit.parents[2] and plistlib.loads((app/'Contents/Info.plist').read_bytes()).get('CFBundleIdentifier')==APP_ID:
            pass  # First run of our own source-installer app copied from a DMG.
        else:raise RuntimeError('Существующее приложение не принадлежит этой установке.')
    shortcuts={}
    for title,command in [('Start Silent Hill 2.command','Start-SH2.command'),
                          ('Update Silent Hill 2.command','Update-SH2.command'),
                          ('Uninstall Silent Hill 2.command','Uninstall-SH2.command')]:
        path=apps/title
        data=('#!/bin/bash\n# '+APP_ID+'\nexec /bin/bash '+shlex.quote(str(app/'Contents/Resources/kit'/command))+' "$@"\n').encode()
        if path.exists() or path.is_symlink():
            previous=read_owned(base/'installation.json').get('shortcuts',{})
            if path.is_symlink() or previous.get(str(path))!=sha(path):
                raise RuntimeError('Неизвестный или изменённый ярлык сохранён: '+str(path))
        shortcuts[path]=data
    metadata=dict(app_id=APP_ID,version=version,base=str(base),app=str(app),kit_sha256=sha(kit/'FILES.sha256'),
        shortcuts={str(p):hashlib.sha256(b).hexdigest() for p,b in shortcuts.items()})
    shortcut_complete=all(p.is_file() and not p.is_symlink() and sha(p)==hashlib.sha256(data).hexdigest()
                          and (p.stat().st_mode&0o111) for p,data in shortcuts.items())
    if app.exists() and read_owned(app_marker(app))==metadata and reg.exists() and read_owned(reg)==metadata and shortcut_complete:
        verify_package(app/'Contents/Resources/kit')
        return dict(version=version,app=str(app),base=str(base),kit=str(app/'Contents/Resources/kit'),actual_mac_launch_verified=False)
    targets=[reg,base/'installation.json',*shortcuts]
    before={p:p.read_bytes() if p.exists() else None for p in targets}
    modes={p:stat.S_IMODE(p.stat().st_mode) for p in targets if p.exists()}
    stage=Path(tempfile.mkdtemp(prefix='.sh2-app-stage-',dir=apps))
    staged=stage/'Silent Hill 2.app'; backup=stage/'previous.app'; installed=False
    try:
        resources=staged/'Contents/Resources/kit'; resources.mkdir(parents=True)
        for name,source in files.items():
            target=resources/name; target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,target);target.chmod(0o755 if name.endswith(('.sh','.command')) else 0o644)
        verify_package(resources)
        binary=staged/'Contents/MacOS/SH2';binary.parent.mkdir()
        binary.write_text('#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")/../Resources/kit" && pwd)\nexec /usr/bin/open -a Terminal "$HERE/Start-SH2.command"\n',encoding='utf-8')
        binary.chmod(0o755)
        info=dict(CFBundleIdentifier=APP_ID,CFBundleName='Silent Hill 2',CFBundleDisplayName='Silent Hill 2',
            CFBundleExecutable='SH2',CFBundlePackageType='APPL',CFBundleShortVersionString=version.split('-')[0],
            CFBundleVersion=str(version_key(version)[-1] or 1),LSMinimumSystemVersion='15.0',
            LSArchitecturePriority=['arm64'],NSHighResolutionCapable=True)
        (staged/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        (staged/'Contents/PkgInfo').write_bytes(b'APPL????')
        put_json(app_marker(staged),metadata)
        if app.exists(): os.replace(app,backup)
        os.replace(staged,app);installed=True
        put_json(base/'installation.json',metadata);put_json(reg,metadata)
        for path,data in shortcuts.items(): atomic(path,data);path.chmod(0o755)
    except BaseException:
        if installed: remove_child(app,apps)
        if backup.exists(): os.replace(backup,app)
        for path,data in before.items():
            if data is None:
                if path.exists() and not path.is_symlink():path.unlink()
            else:atomic(path,data);path.chmod(modes[path])
        raise
    finally:
        remove_child(stage,apps)
    return dict(version=version,app=str(app),base=str(base),kit=str(app/'Contents/Resources/kit'),
                actual_mac_launch_verified=False)

def install(kit, home, requested=None):
    kit=canonical(kit);base=location(home,requested);verify_package(kit)
    reg=registry(home)
    if reg.exists() and read_owned(reg)['base']!=str(base):
        raise RuntimeError('Уже используется другая папка данных; сначала удалите прежнюю установку.')
    base=init_base(base,home)
    with locked(base):return install_locked(kit,base,home)

def version_key(version):
    m=re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?',version)
    if not m:return None
    a,b,c,channel,n=m.groups()
    return tuple(map(int,(a,b,c)))+({'alpha':0,'beta':1,'rc':2,None:3}[channel],int(n or 0))

def select_release(releases,current):
    chosen=[];current_key=version_key(current)
    if current_key is None:raise RuntimeError('Неизвестная текущая версия.')
    for release in releases:
        if release.get('draft'):continue
        tag=release.get('tag_name','');version=tag.removeprefix('v') if hasattr(str,'removeprefix') else tag[1:] if tag.startswith('v') else tag
        key=version_key(version)
        if key is None or key<=current_key or (current_key[-2]==3 and release.get('prerelease')):continue
        name='SH2-macOS-'+version+'-Source-Installer.zip'
        for asset in release.get('assets',[]):
            if asset.get('name')!=name:continue
            expected='https://github.com/'+REPO+'/releases/download/'+tag+'/'+name
            if asset.get('browser_download_url')!=expected:raise RuntimeError('Неизвестный адрес архива обновления.')
            digest=asset.get('digest','')
            if not re.fullmatch('sha256:[0-9a-f]{64}',digest):raise RuntimeError('GitHub не предоставил SHA-256 обновления.')
            if not 0<asset.get('size',0)<=MAX_ARCHIVE:raise RuntimeError('Некорректный размер обновления.')
            chosen.append((key,dict(version=version,url=expected,sha256=digest[7:],bytes=asset['size'])))
    return max(chosen,key=lambda x:x[0])[1] if chosen else None

def fetch(url,limit):
    request=Request(url,headers={'User-Agent':'SH2-macOS-installer','Accept':'application/vnd.github+json'})
    with urlopen(request,timeout=45) as response:
        end=urlparse(response.geturl())
        if end.scheme!='https' or end.hostname not in ('api.github.com','github.com','release-assets.githubusercontent.com'):
            raise RuntimeError('Неизвестное перенаправление загрузки.')
        data=response.read(limit+1)
    if len(data)>limit:raise RuntimeError('Слишком большой ответ сервера.')
    return data

def extract_update(data,folder,digest):
    if len(data)>MAX_ARCHIVE or hashlib.sha256(data).hexdigest()!=digest:raise RuntimeError('SHA-256 обновления не совпала.')
    import io
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        infos=z.infolist()
        if len(infos)>1500 or sum(i.file_size for i in infos)>40*1024**2:raise RuntimeError('Слишком большой распакованный комплект.')
        seen=set();prefix=None
        for i in infos:
            p=PurePosixPath(i.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in i.filename or ':' in i.filename or not p.parts:
                raise RuntimeError('Небезопасный путь ZIP.')
            if prefix is None:prefix=p.parts[0]
            if p.parts[0]!=prefix or stat.S_ISLNK(i.external_attr>>16):raise RuntimeError('Ссылка или несколько корней ZIP.')
            name=p.as_posix().casefold()
            if name in seen:raise RuntimeError('Повтор пути ZIP.')
            seen.add(name)
        for i in infos:
            if i.is_dir():continue
            p=folder.joinpath(*PurePosixPath(i.filename).parts);p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(z.read(i))
    kit=folder/prefix;verify_package(kit)
    return kit

def update(home):
    base=location(home);value=read_owned(base/'installation.json')
    with locked(base):
        releases=json.loads(fetch('https://api.github.com/repos/'+REPO+'/releases?per_page=20',1024**2))
        release=select_release(releases,value['version'])
        if release is None:return dict(updated=False,version=value['version'],message='Уже установлена последняя доступная версия.')
        updates=canonical(base/'updates');updates.mkdir(exist_ok=True)
        work=Path(tempfile.mkdtemp(prefix='.download-',dir=updates))
        try:
            data=fetch(release['url'],MAX_ARCHIVE)
            if len(data)!=release['bytes']:raise RuntimeError('Размер обновления не совпал.')
            kit=extract_update(data,work,release['sha256'])
            if (kit/'VERSION').read_text().strip()!=release['version']:raise RuntimeError('Версия архива не совпала.')
            result=install_locked(kit,base,home);result['updated']=True
            return result
        finally:remove_child(work,updates)

def uninstall(home,remove_saves=False,confirmed=False):
    if not confirmed:raise RuntimeError('Удаление требует явного подтверждения.')
    base=location(home);value=read_owned(base/'installation.json');base=safe_base(base,home)
    app=canonical(value['app']);apps=canonical(Path(home)/'Applications')
    if app!=apps/'Silent Hill 2.app':raise RuntimeError('Неизвестное приложение.')
    if app.exists() and read_owned(app_marker(app)).get('base')!=str(base):raise RuntimeError('Приложение относится к другой установке.')
    save_export=None;preserved=[]
    exports=canonical(Path(home)/'Library/Application Support/SH2-brkovv-Saves')
    with locked(base):
        # Validate every deletion root before deleting anything.
        targets=[base/name for name in DATA_NAMES if (base/name).exists() or (base/name).is_symlink()]
        for target in targets:
            if target.is_symlink() or canonical(target)!=target:raise RuntimeError('Ссылка вместо папки данных: '+str(target))
        previous_exports=[]
        if remove_saves and exports.exists():
            for child in exports.iterdir():
                if child.is_dir() and not child.is_symlink() and (child/'installation.json').is_file():
                    try:marker=read_owned(child/'installation.json')
                    except (RuntimeError,ValueError):marker={}
                    if marker.get('kind')=='saved-progress':previous_exports.append(child);continue
                preserved.append(str(child))
        if not remove_saves:
            sources=[]
            game=base/'game'
            if game.is_dir():
                for name in ('data/save','keyconf.dat','sh2e.ini','language.ini','device.fix'):
                    p=game/name
                    if p.exists():sources.append((p,Path('game')/name))
            if (base/'backups').is_dir():sources.append((base/'backups',Path('backups')))
            for source,_ in sources:
                if source.is_symlink() or canonical(source)!=source or (source.is_dir() and any(p.is_symlink() for p in source.rglob('*'))):
                    raise RuntimeError('Ссылки в сохранениях не копируются.')
            if sources:
                exports.mkdir(parents=True,exist_ok=True)
                save_export=exports/datetime.now().strftime('%Y%m%d-%H%M%S-%f');save_export.mkdir()
                put_json(save_export/'installation.json',dict(app_id=APP_ID,kind='saved-progress'))
                for source,relative in sources:
                    dst=save_export/relative;dst.parent.mkdir(parents=True,exist_ok=True)
                    if source.is_dir():shutil.copytree(source,dst)
                    else:shutil.copy2(source,dst)
        if app.exists():remove_child(app,apps)
        for name,digest in value.get('shortcuts',{}).items():
            p=Path(name)
            allowed={'Start Silent Hill 2.command','Update Silent Hill 2.command','Uninstall Silent Hill 2.command'}
            if p.parent==apps and p.name in allowed and not p.is_symlink() and p.is_file() and sha(p)==digest:p.unlink()
            elif p.exists():preserved.append(str(p))
        for target in targets:remove_child(target,base)
        for previous in previous_exports:remove_child(previous,exports)
        if remove_saves and exports.exists():
            try:exports.rmdir()
            except OSError:preserved.append(str(exports))
        reg=registry(home)
        if reg.exists() and read_owned(reg).get('base')==str(base):reg.unlink()
        (base/'installation.json').unlink()
    (base/'.install.lock').unlink()
    try:base.rmdir()
    except OSError:preserved.append(str(base))
    return dict(uninstalled=True,saves_removed=remove_saves,saves_export=str(save_export) if save_export else None,
                preserved_unknown_paths=preserved,shared_homebrew_and_apple_tools_retained=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['install','location','update','uninstall'])
    parser.add_argument('--base',type=Path)
    parser.add_argument('--remove-saves',action='store_true')
    parser.add_argument('--yes',action='store_true')
    args=parser.parse_args();home=Path.home()
    if args.action=='location':print(location(home,args.base));return
    if sys.platform!='darwin':parser.error('Команды установки выполняются на macOS; переносимые функции отдельно проверяются на Linux.')
    if args.action=='install':result=install(Path(__file__).resolve().parent,home,args.base)
    elif args.action=='update':result=update(home)
    else:
        base=location(home);value=read_owned(base/'installation.json')
        print('Приложение: '+value['app']);print('Данные: '+str(base))
        if not args.yes:
            print('1 — удалить программу и сборки, сохранить прохождение\n2 — удалить ВСЁ из установки, включая сохранения и их резервные копии\nEnter — отмена')
            choice=input('Выбор: ').strip()
            if choice not in ('1','2'):print('Отменено.');return
            args.remove_saves=choice=='2'
            if args.remove_saves and input('Для полного удаления напишите DELETE SH2: ').strip()!='DELETE SH2':print('Отменено.');return
        result=uninstall(home,args.remove_saves,confirmed=True)
    print(json.dumps(result,indent=2,ensure_ascii=False))

if __name__=='__main__':
    try:main()
    except (Exception,KeyboardInterrupt) as error:print('Ошибка: '+str(error),file=sys.stderr);raise SystemExit(2)
