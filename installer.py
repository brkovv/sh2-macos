"""Bring-your-own-game source installer. Never downloads a game or ships lifted C."""
import argparse
from datetime import datetime
import hashlib,importlib.util,json,os,platform,shutil,subprocess,sys,tempfile
from pathlib import Path,PurePosixPath

EXPECTED_EXE='f31a221779fadca4dae8fe758546ecde8b88a88fa6e80fad7dee115d2ef620e8'
GAME_ITEMS=('sh2pc.exe','data','sh2e','lang','translations','keyconf.dat','sh2e.ini','language.ini','device.fix')

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def validate_game(game):
    game=game.expanduser().resolve(strict=True)
    exe=game/'sh2pc.exe'
    if not exe.is_file() or exe.is_symlink() or sha(exe)!=EXPECTED_EXE:
        raise RuntimeError('Нужен поддерживаемый EXE Enhanced Edition. Оригинальная папка не изменена.')
    for name in ('data','sh2e'):
        if not (game/name).is_dir() or (game/name).is_symlink():raise RuntimeError('Нет обычной папки '+name)
    for name in ('data/sound/sddata.bin','sh2e/pic/etc/start00.tex','sh2e/menu/mc/savebg.tbn2'):
        if not (game/name).is_file():raise RuntimeError('Не хватает ресурса Enhanced Edition: '+name)
    return game

def verify_source(package):
    manifest=json.loads((package/'SOURCE-MANIFEST.json').read_text())
    source=package/'source'
    for relative,digest in manifest['files'].items():
        p=PurePosixPath(relative)
        if p.is_absolute() or '..' in p.parts or '\\' in relative or p.as_posix()!=relative:
            raise RuntimeError('Некорректный путь исходника: '+relative)
        target=source.joinpath(*p.parts)
        if target.resolve()!=target or not target.is_file() or sha(target)!=digest:
            raise RuntimeError('Исходник повреждён или заменён ссылкой: '+relative)
    found={p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file() or p.is_symlink()}
    if found!=set(manifest['files']):raise RuntimeError('Неизвестные файлы в исходном комплекте.')
    return manifest,sha(package/'SOURCE-MANIFEST.json')

def import_game(original,base):
    original=validate_game(original)
    target=base/'game'
    if target.resolve()!=target:raise RuntimeError('Ссылка вместо установленной игровой папки.')
    if target.exists():return validate_game(target)
    paths=[];total=0
    for name in GAME_ITEMS:
        item=original/name
        if not item.exists() and not item.is_symlink():continue
        for p in [item]+(list(item.rglob('*')) if item.is_dir() else []):
            if p.is_symlink():raise RuntimeError('Ссылки в игровой папке не импортируются: '+str(p))
            if p.is_file():total+=p.stat().st_size
        paths.append(item)
    base.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(base).free<total+2*1024**3:raise RuntimeError('Недостаточно места для копии игры и сборки.')
    stage=Path(tempfile.mkdtemp(prefix='.game-import-',dir=base))
    try:
        for item in paths:
            if item.is_dir():shutil.copytree(item,stage/item.name)
            else:shutil.copy2(item,stage/item.name)
        validate_game(stage)
        if target.exists():raise RuntimeError('Папка игры появилась во время импорта; она сохранена.')
        os.rename(stage,target)
    finally:
        if stage.exists():
            assert stage.resolve().parent==base.resolve() and stage.name.startswith('.game-import-')
            shutil.rmtree(stage)
    return target

def snapshot(game,base):
    backup=base/'backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    for p in game.iterdir():
        if p.is_file() and p.suffix.lower() in ('.ini','.dat','.fix','.bin','.log'):shutil.copy2(p,backup/p.name)
    if (game/'data/save').is_dir():shutil.copytree(game/'data/save',backup/'data-save')
    return backup

def defaults(game):
    p=game/'sh2e.ini';old=p.read_bytes() if p.exists() else b''
    present=set()
    for line in old.decode('utf-8',errors='replace').splitlines():
        if '=' in line and not line.lstrip().startswith(';'):present.add(line.split('=',1)[0].strip())
    values=dict(resolution='auto',fullscreen='1',mouse_mode='1',fps='60',vsync='0',movie_scale='2',cores='3',text_language='1')
    extra=''.join(f'{k}={v}\n' for k,v in values.items() if k not in present)
    if extra:p.write_bytes(old+b'\n; brkovv desktop defaults (existing values retained)\n'+extra.encode())

def prepare_source(package,base):
    manifest,digest=verify_source(package)
    source=base/'versions'/(manifest['version']+'-'+digest[:16])/'source'
    if source.parent.resolve()!=source.parent:raise RuntimeError('Ссылка вместо папки установленных версий.')
    if source.exists():
        for relative,expected in manifest['files'].items():
            p=source/relative
            if p.resolve()!=p or not p.is_file() or sha(p)!=expected:raise RuntimeError('Установленные исходники изменены: '+relative)
    else:
        source.parent.mkdir(parents=True,exist_ok=True)
        shutil.copytree(package/'source',source)
    for p in source.rglob('*.sh'):p.chmod(0o755)
    link=source/'game'
    if link.is_symlink():
        if link.resolve()!=base/'game':raise RuntimeError('Неизвестная ссылка source/game.')
    elif link.exists():raise RuntimeError('Неизвестная папка source/game.')
    else:link.symlink_to(base/'game',target_is_directory=True)
    return source,digest

def run_logged(args,source,env,path):
    print(' '.join(map(str,args)),flush=True)
    with path.open('wb') as log:
        code=subprocess.run(list(map(str,args)),cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
    if code:raise RuntimeError(f'Этап завершился кодом {code}. Лог: {path}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path)
    parser.add_argument('--base',type=Path,default=Path.home()/'Library/Application Support/SH2-brkovv')
    parser.add_argument('--validate-only',action='store_true')
    parser.add_argument('--russian',action='store_true')
    args=parser.parse_args();package=Path(__file__).resolve().parent
    verify_source(package)
    if args.validate_only:
        if not args.game:parser.error('--game required for validation')
        validate_game(args.game);print('PASS: supported EE game; no files changed.');return
    if sys.platform!='darwin' or platform.machine()!='arm64':parser.error('Установка/запуск нужны на Apple Silicon Mac; здесь доступна только --validate-only.')
    import fcntl
    base=args.base.expanduser().resolve();base.mkdir(parents=True,exist_ok=True)
    with (base/'.install.lock').open('w') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise RuntimeError('Установка или игра уже открыты.')
        lock.write(str(os.getpid()));lock.flush()
        game=validate_game(base/'game') if (base/'game').exists() else import_game(args.game,base) if args.game else None
        if game is None:parser.error('Выберите свою папку игры с Enhanced Edition.')
        backup=snapshot(game,base);print('Резервная копия настроек/сохранений: '+str(backup),flush=True)
        defaults(game)
        if args.russian:
            spec=importlib.util.spec_from_file_location('russian',package/'install-russian-pack.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            module.install(package,base,module.obtain(package,base/'russian-download-cache'))
            p=game/'sh2e.ini';p.write_bytes(p.read_bytes()+b'\ntext_language=6\n')
        source,digest=prepare_source(package,base)
        logdir=base/'logs'/datetime.now().strftime('%Y%m%d-%H%M%S-%f');logdir.mkdir(parents=True)
        env=dict(os.environ,SH2_PYTHON=sys.executable,SH2_JOBS=os.environ.get('SH2_JOBS','2'))
        marker=source.parent/'generated.json'
        generated=dict(source_manifest=digest,exe=EXPECTED_EXE,capstone='5.0.7')
        if not marker.is_file() or json.loads(marker.read_text())!=generated or not (source/'src/recomp/gen/recomp_dispatch.c').is_file():
            import capstone
            if capstone.__version__!='5.0.7':raise RuntimeError('Нужен capstone 5.0.7 в изолированной среде установщика.')
            run_logged(['sh','tools/regen.sh'],source,env,logdir/'generate.log')
            marker.write_text(json.dumps(generated,indent=2)+'\n')
        run_logged(['sh','tools/build-macos.sh'],source,env,logdir/'build.log')
        print('Сборка завершена. Запускаем игру. Логи: '+str(logdir),flush=True)
        # The existing native runner snapshots again and records actual Mac diagnostics.
        result=subprocess.run([sys.executable,str(source/'tools/test-macos.py'),str(game),'--logs-dir',str(logdir)],env=env)
        if result.returncode:raise RuntimeError('Игра завершилась с ошибкой; логи: '+str(logdir))

if __name__=='__main__':
    try:main()
    except Exception as error:print('Ошибка: '+str(error),file=sys.stderr);raise SystemExit(2)
