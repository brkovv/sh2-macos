"""Exercise real installation/update/removal helpers with isolated non-game fixtures."""
from pathlib import Path
import hashlib, importlib.util, io, json, os, shutil, stat, subprocess, sys, tempfile, unittest, zipfile
from unittest.mock import patch

sys.dont_write_bytecode=True
KIT=Path(__file__).resolve().parents[1]
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,KIT/file)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
m=load('sh2_manage','manage-install.py');runtime=load('sh2_runtime_installer','installer.py')

def checksums(kit):
    files={p.relative_to(kit).as_posix():p for p in kit.rglob('*') if p.is_file() and p.name!='FILES.sha256'}
    (kit/'FILES.sha256').write_text(''.join(m.sha(p)+'  '+n+'\n' for n,p in sorted(files.items())))

def archive(kit):
    data=io.BytesIO()
    with zipfile.ZipFile(data,'w',zipfile.ZIP_DEFLATED) as z:
        for p in kit.rglob('*'):
            if p.is_file():z.write(p,'SH2-test/'+p.relative_to(kit).as_posix())
    return data.getvalue()

class Installation(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='sh2-install-test-')
        self.work=Path(self.tmp.name);self.home=self.work/'home with spaces';self.home.mkdir()
    def tearDown(self):self.tmp.cleanup()
    def install(self,base=None):
        result=m.install(KIT,self.home,base);self.base=Path(result['base']);self.app=Path(result['app']);return result
    def saves(self):
        for name,data in [('game/data/save/Folder 01/save001.dat',b'progress'),('game/keyconf.dat',b'bindings'),
                          ('game/sh2e.ini',b'fps=30\nmouse_mode=0\n'),('backups/old/progress.dat',b'old progress')]:
            p=self.base/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    def nextkit(self):
        kit=self.work/'next-kit';shutil.copytree(KIT,kit)
        current=(KIT/'VERSION').read_text().strip();key=m.version_key(current)
        version='.'.join(map(str,key[:3]))+'-alpha.'+str(key[-1]+1)
        (kit/'VERSION').write_text(version+'\n')
        for name in ('SOURCE-MANIFEST.json','RELEASE_STATUS.json'):
            p=kit/name;data=json.loads(p.read_text());data['version']=version;p.write_text(json.dumps(data,indent=2)+'\n')
        checksums(kit);return kit,version
    def test_install_repeat_custom_and_owned_shortcuts(self):
        base=self.home/'Games/custom SH2';result=self.install(base);self.saves()
        self.assertEqual(m.location(self.home),base)
        before=self.app.stat().st_mtime_ns
        self.assertEqual(m.install(KIT,self.home),result)
        self.assertEqual(self.app.stat().st_mtime_ns,before)
        self.assertEqual((base/'game/keyconf.dat').read_bytes(),b'bindings')
        for name in ('Start','Update','Uninstall'):
            self.assertTrue((self.home/'Applications'/(name+' Silent Hill 2.command')).is_file())
        m.verify_package(self.app/'Contents/Resources/kit')
        missing=self.home/'Applications/Update Silent Hill 2.command';missing.unlink()
        m.install(KIT,self.home)
        self.assertTrue(missing.is_file());self.assertTrue(missing.stat().st_mode&0o111)
    def test_finder_metadata_is_ignored_but_not_symlinks(self):
        kit=self.work/'finder-kit';shutil.copytree(KIT,kit)
        (kit/'.DS_Store').write_bytes(b'Finder metadata')
        (kit/'source/.DS_Store').write_bytes(b'Finder source metadata')
        result=m.install(kit,self.home);runtime.verify_source(kit)
        installed=Path(result['kit']);self.assertFalse((installed/'.DS_Store').exists())
        (installed/'.DS_Store').write_bytes(b'Finder metadata')
        self.assertEqual(m.install(KIT,self.home)['version'],result['version'])
        (kit/'.DS_Store').unlink();outside=self.work/'outside';outside.write_bytes(b'user')
        (kit/'.DS_Store').symlink_to(outside)
        with self.assertRaises(RuntimeError):m.verify_package(kit)
    def test_dangerous_paths_foreign_app_and_symlinks(self):
        for base in (self.home,Path('/'),self.home/'Library',self.home/'Applications',self.home/'Applications/Silent Hill 2.app'):
            with self.assertRaises(RuntimeError):m.install(KIT,self.home,base)
        foreign=self.home/'Applications/Silent Hill 2.app';foreign.mkdir(parents=True);(foreign/'keep').write_bytes(b'original')
        with self.assertRaises(RuntimeError):m.install(KIT,self.home)
        self.assertEqual((foreign/'keep').read_bytes(),b'original')
        outside=self.work/'outside';outside.mkdir();link=self.home/'linked';link.symlink_to(outside,target_is_directory=True)
        with self.assertRaises(RuntimeError):m.install(KIT,self.home,link)
        self.assertEqual(list(outside.iterdir()),[])
    def test_lock_blocks_update_install_and_uninstall(self):
        self.install();self.saves()
        with m.locked(self.base):
            with self.assertRaises(RuntimeError):m.install(KIT,self.home)
            with self.assertRaises(RuntimeError):m.uninstall(self.home,True,True)
        self.assertTrue(self.app.exists());self.assertTrue((self.base/'game/keyconf.dat').exists())
    def test_install_rollback_after_app_swap_failure(self):
        self.install();self.saves();kit,_=self.nextkit()
        before=(m.registry(self.home).read_bytes(),(self.app/'Contents/Resources/kit/VERSION').read_bytes())
        original=m.os.replace;injected=[]
        def fail(src,dst):
            if Path(dst)==self.app and Path(src).name=='Silent Hill 2.app' and not injected:
                injected.append(True);raise OSError('injected app replacement failure')
            return original(src,dst)
        with patch.object(m.os,'replace',fail):
            with self.assertRaises(OSError):m.install(kit,self.home)
        self.assertEqual(before,(m.registry(self.home).read_bytes(),(self.app/'Contents/Resources/kit/VERSION').read_bytes()))
        self.assertEqual((self.base/'game/keyconf.dat').read_bytes(),b'bindings')
    def test_update_verified_zip_preserves_game_and_source_cache(self):
        self.install();self.saves();kit,version=self.nextkit();data=archive(kit)
        old_source,old_hash=runtime.prepare_source(KIT,self.base)
        next_source,next_hash=runtime.prepare_source(kit,self.base)
        self.assertEqual((old_source,old_hash),(next_source,next_hash))
        asset=dict(name='SH2-macOS-'+version+'-Source-Installer.zip',digest='sha256:'+hashlib.sha256(data).hexdigest(),size=len(data))
        asset['browser_download_url']='https://github.com/'+m.REPO+'/releases/download/v'+version+'/'+asset['name']
        listing=[dict(tag_name='v'+version,draft=False,prerelease=True,assets=[asset])]
        def fetch(url,limit):return json.dumps(listing).encode() if url.startswith('https://api.github.com/') else data
        with patch.object(m,'fetch',fetch):result=m.update(self.home)
        self.assertTrue(result['updated']);self.assertEqual(result['version'],version)
        self.assertEqual((self.base/'game/sh2e.ini').read_bytes(),b'fps=30\nmouse_mode=0\n')
        self.assertTrue((old_source/'game').is_symlink())
        with patch.object(m,'fetch',lambda url,limit:json.dumps(listing).encode()):
            self.assertFalse(m.update(self.home)['updated'])
    def test_update_hash_failure_keeps_installed_version(self):
        self.install();kit,version=self.nextkit();data=archive(kit)
        name='SH2-macOS-'+version+'-Source-Installer.zip'
        listing=[dict(tag_name='v'+version,prerelease=True,assets=[dict(name=name,size=len(data),digest='sha256:'+'0'*64,
            browser_download_url='https://github.com/'+m.REPO+'/releases/download/v'+version+'/'+name)])]
        before=m.registry(self.home).read_bytes()
        with patch.object(m,'fetch',lambda url,limit:json.dumps(listing).encode() if 'api.github.com' in url else data):
            with self.assertRaises(RuntimeError):m.update(self.home)
        self.assertEqual(m.registry(self.home).read_bytes(),before)
        self.assertEqual(list((self.base/'updates').iterdir()),[])
    def test_zip_traversal_duplicate_symlink_and_corruption(self):
        for entries in ([('../escape',b'x',False)],[('root/a',b'x',False),('root/A',b'y',False)],[('root/link',b'/tmp',True)]):
            b=io.BytesIO()
            with zipfile.ZipFile(b,'w') as z:
                for name,content,link in entries:
                    item=zipfile.ZipInfo(name);item.create_system=3
                    item.external_attr=(stat.S_IFLNK|0o777)<<16 if link else (stat.S_IFREG|0o644)<<16
                    z.writestr(item,content)
            data=b.getvalue();folder=self.work/('extract-'+str(len(list(self.work.iterdir()))));folder.mkdir()
            with self.assertRaises(RuntimeError):m.extract_update(data,folder,hashlib.sha256(data).hexdigest())
            self.assertEqual(list(folder.iterdir()),[])
        with self.assertRaises(RuntimeError):m.extract_update(b'invalid',self.work,'0'*64)
    def test_uninstall_keep_saves_then_full_cleanup_and_external_link(self):
        self.install();self.saves()
        outside=self.work/'original-game';outside.mkdir();(outside/'keep').write_bytes(b'not ours')
        versions=self.base/'versions';versions.mkdir();(versions/'external-link').symlink_to(outside,target_is_directory=True)
        result=m.uninstall(self.home,False,True)
        export=Path(result['saves_export'])
        self.assertEqual((export/'game/data/save/Folder 01/save001.dat').read_bytes(),b'progress')
        self.assertFalse(self.app.exists());self.assertFalse(self.base.exists());self.assertFalse(m.registry(self.home).exists())
        self.assertEqual((outside/'keep').read_bytes(),b'not ours')
        self.install();self.saves();m.uninstall(self.home,True,True)
        self.assertFalse(export.exists());self.assertFalse(self.base.exists());self.assertEqual((outside/'keep').read_bytes(),b'not ours')
    def test_uninstall_requires_confirmation_preserves_foreign_files(self):
        self.install();self.saves()
        with self.assertRaises(RuntimeError):m.uninstall(self.home,True)
        (self.base/'unrelated-user-file.txt').write_bytes(b'user')
        shortcut=self.home/'Applications/Update Silent Hill 2.command';shortcut.write_bytes(b'user changed shortcut')
        result=m.uninstall(self.home,True,True)
        self.assertEqual((self.base/'unrelated-user-file.txt').read_bytes(),b'user')
        self.assertEqual(shortcut.read_bytes(),b'user changed shortcut')
        self.assertIn(str(self.base),result['preserved_unknown_paths'])
    def test_linked_data_root_is_refused_before_deletion(self):
        self.install();outside=self.work/'external-game';outside.mkdir();(outside/'keep').write_bytes(b'user')
        (self.base/'game').symlink_to(outside,target_is_directory=True)
        with self.assertRaises(RuntimeError):m.uninstall(self.home,True,True)
        self.assertTrue(self.app.exists());self.assertEqual((outside/'keep').read_bytes(),b'user')
    def test_streamed_build_log_uses_real_child_process(self):
        log=self.work/'build.log'
        runtime.run_logged([sys.executable,'-c','print("visible build progress")'],self.work,dict(os.environ),log)
        self.assertIn(b'visible build progress',log.read_bytes())
        with self.assertRaises(RuntimeError):runtime.run_logged([sys.executable,'-c','raise SystemExit(7)'],self.work,dict(os.environ),log)

if __name__=='__main__':unittest.main(verbosity=2)
