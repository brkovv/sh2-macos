"""Download only a pinned public translation; validate every byte before private installation."""
from datetime import datetime
import hashlib,json,os,shutil,sys,tempfile,urllib.request,zipfile
from pathlib import Path

def digest(data): return hashlib.sha256(data).hexdigest()

def prepare(package, archive):
    spec=json.loads((package/'RUSSIAN-PACK.json').read_text())
    if digest(archive.read_bytes())!=spec['sha256']: raise RuntimeError('SHA-256 архива перевода не совпала.')
    files={}
    with zipfile.ZipFile(archive) as bundle:
        for relative,meta in spec['files'].items():
            path=Path(relative)
            if path.is_absolute() or '..' in path.parts or '\\' in relative: raise RuntimeError('Некорректный путь перевода.')
            item=bundle.getinfo(spec['prefix']+relative)
            if item.file_size!=meta['bytes'] or item.file_size>20*1024*1024: raise RuntimeError('Некорректный размер перевода.')
            data=bundle.read(item)
            if digest(data)!=meta['sha256']: raise RuntimeError('Повреждён файл перевода: '+relative)
            files[relative]=data
    rows=files['etc/resource/r_menu_g.res'].decode('utf-8-sig').splitlines()[:9]
    if len(rows)!=9 or any(not row.startswith('\\h') for row in rows): raise RuntimeError('Некорректное меню перевода.')
    files['pause.bin']=b''.join(row.replace('ё','е').replace('Ё','Е').encode('cp1251')+b'\0' for row in rows)
    files['provenance.json']=(json.dumps(dict(repository=spec['repository'],commit=spec['commit'],archive_sha256=spec['sha256'],credits='Metallist / Hellbringer / ilyuXer'),indent=2)+'\n').encode()
    return files

def obtain(package, cache):
    spec=json.loads((package/'RUSSIAN-PACK.json').read_text())
    if cache.resolve()!=cache: raise RuntimeError('Ссылка в пути кэша перевода.')
    cache.mkdir(parents=True,exist_ok=True)
    archive=cache/'russian-text-pinned.zip'
    if archive.is_symlink(): raise RuntimeError('Ссылка вместо архива перевода.')
    if not archive.is_file() or digest(archive.read_bytes())!=spec['sha256']:
        print('Скачиваем текстовый перевод Metallist / ilyuXer с GitHub…',flush=True)
        fd,name=tempfile.mkstemp(prefix='.russian-download-',dir=cache)
        try:
            request=urllib.request.Request(spec['url'],headers={'User-Agent':'SH2-private-text-installer'})
            with os.fdopen(fd,'wb') as out,urllib.request.urlopen(request,timeout=45) as response:
                total=0
                while block:=response.read(1024*1024):
                    total+=len(block)
                    if total>32*1024*1024: raise RuntimeError('Архив перевода превышает ожидаемый размер.')
                    out.write(block)
            if digest(Path(name).read_bytes())!=spec['sha256']: raise RuntimeError('SHA-256 скачанного перевода не совпала.')
            os.replace(name,archive)
        finally: Path(name).unlink(missing_ok=True)
    prepare(package,archive)
    return archive

def preflight(target,files):
    parent=target/'game/translations';destination=parent/'russian'
    if parent.resolve()!=parent or destination.resolve()!=destination: raise RuntimeError('Ссылка в пути перевода.')
    lang=target/'game/language.ini'
    if lang.resolve()!=lang or (lang.exists() and not lang.is_file()): raise RuntimeError('Ссылка в пути language.ini.')
    if destination.exists():
        if not destination.is_dir(): raise RuntimeError('Файл вместо папки перевода.')
        found={p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
        if found!=set(files) or any((destination/p).is_symlink() or (destination/p).read_bytes()!=data for p,data in files.items()):
            raise RuntimeError('Папка русского перевода изменена. Файлы сохранены без изменений.')
    return destination.exists()

def install(package, target, archive):
    target=target.resolve(strict=True)
    files=prepare(package,archive)
    parent=target/'game/translations';destination=parent/'russian'
    if preflight(target,files):
        print('Проверенный русский перевод уже установлен.',flush=True)
        return
    parent.mkdir(parents=True,exist_ok=True)
    staged=Path(tempfile.mkdtemp(prefix='.russian-stage-',dir=parent))
    try:
        for relative,data in files.items():
            output=staged/relative;output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
        backup_root=target/'language-backups'
        if backup_root.resolve()!=backup_root: raise RuntimeError('Ссылка в пути резервной копии.')
        backup=backup_root/('russian-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        backup.mkdir(parents=True)
        lang=target/'game/language.ini'
        if lang.exists():
            if lang.resolve()!=lang or not lang.is_file(): raise RuntimeError('Ссылка в пути language.ini.')
            shutil.copy2(lang,backup/'language.ini')
        (backup/'new-overlay.txt').write_text(str(destination)+'\n')
        os.replace(staged,destination)
        print('Русский текст установлен отдельно: '+str(destination),flush=True)
        print('Резервная копия language.ini: '+str(backup),flush=True)
    finally:
        if staged.exists(): shutil.rmtree(staged)

def main():
    package=Path(sys.argv[1]).resolve(strict=True);target=Path(sys.argv[2]).resolve(strict=True)
    # obtain/prepare never change the installed game. Source updater validates its
    # own payload, locks and hashes before invoking this script's install mode.
    archive=obtain(package,target.parent/'russian-download-cache')
    preflight(target,prepare(package,archive))
    if len(sys.argv)>3 and sys.argv[3]=='--download-only': return
    install(package,target,archive)

if __name__=='__main__':
    try: main()
    except Exception as error:
        print('Ошибка перевода: '+str(error),file=sys.stderr);raise SystemExit(2)
