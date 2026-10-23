"""Offline source-release lint. --publish-check also enforces recorded readiness gates."""
import argparse,ast,hashlib,importlib.util,json,re,sys
from pathlib import Path

def check(root,publish=False,prerelease=False):
    spec=importlib.util.spec_from_file_location('installer',root/'installer.py')
    installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
    installer.verify_source(root)
    found=[]
    token=re.compile(rb'\bgh[pousr]_[A-Za-z0-9_]{25,}\b|\bAKIA[A-Z0-9]{16}\b|-----BEGIN (?:RSA |OPENSSH )?PRIVATE KEY-----')
    for path in sorted(root.rglob('*')):
        relative=path.relative_to(root)
        if '.git' in relative.parts:continue  # Normal GitHub checkout metadata is not a payload file.
        if path.is_symlink():raise RuntimeError('Symlink in candidate: '+str(relative))
        if not path.is_file():continue
        if '__pycache__' in relative.parts:continue
        if relative.parts[0]=='game' or any(x in relative.parts for x in ('.git','gen','build','private','logs')) or path.suffix.lower() in ('.exe','.dll','.xbe','.dylib','.so','.o','.dat','.tex','.tbn2','.mes','.f32','.wav','.bik'):
            raise RuntimeError('Excluded release file: '+str(relative))
        data=path.read_bytes()
        if data.startswith((b'MZ',b'\x7fELF',bytes.fromhex('cffaedfe'))):raise RuntimeError('Executable payload: '+str(relative))
        if token.search(data):raise RuntimeError('Possible credential: '+str(relative))
        if path.name=='.env' or path.name.startswith('.env.'):raise RuntimeError('Environment file: '+str(relative))
        if path.suffix=='.py':ast.parse(data,filename=str(relative))
        found.append(dict(path=relative.as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    status=json.loads((root/'RELEASE_STATUS.json').read_text())
    reasons=[]
    if not status.get('upstream_redistribution_permission'):reasons.append('Upstream redistribution permission is pending.')
    if not status.get('license') or not (root/'LICENSE').is_file():reasons.append('Repository license is missing.')
    if not status.get('third_party_notices_reviewed'):reasons.append('Third-party notices need review.')
    warnings=[]
    if not status.get('actual_mac_clean_install_test'):warnings.append('Actual clean Mac installation test is pending.')
    if not status.get('full_game_regression_test'):warnings.append('Full-game regression coverage is pending for a stable release.')
    if prerelease:
        readme=(root/'README.md').read_text(encoding='utf-8')
        if 'Experimental alpha' not in readme or 'has not yet been verified' not in readme:
            reasons.append('Prerelease limitations must be disclosed in the README.')
    else:reasons.extend(warnings)
    result=dict(content_lint_passed=True,files=len(found),bytes=sum(f['bytes'] for f in found),
        release_channel='prerelease' if prerelease else 'stable',ready_for_publication=not reasons,publication_blockers=reasons,validation_warnings=warnings)
    if publish and reasons:raise RuntimeError('\n'.join(reasons))
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--publish-check',action='store_true');parser.add_argument('--prerelease',action='store_true');args=parser.parse_args()
    try:print(json.dumps(check(Path(__file__).resolve().parent,args.publish_check,args.prerelease),indent=2))
    except Exception as error:print(str(error),file=sys.stderr);raise SystemExit(2)
