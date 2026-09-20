"""Freeze and verify local SD resources independently of firmware installation.

Manifest is a reproducibility/checksum record, not an authorization trust root.
This command never writes SD. Verification exits nonzero for missing/mutated files.
"""
import argparse,hashlib,json
from pathlib import Path,PurePosixPath

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(65536),b''):h.update(chunk)
    return h.hexdigest()

def verify(root,manifest):
    if manifest.get('schema')!=1:raise ValueError('Unsupported resource manifest')
    errors=[]
    for row in manifest['files']:
        rel=PurePosixPath(row['path'])
        if rel.is_absolute() or '..' in rel.parts or not rel.parts or rel.parts[0]!='paper':raise ValueError('Unsafe resource path')
        path=root.joinpath(*rel.parts)
        if not path.resolve().is_relative_to(root.resolve()):raise ValueError('Resource escapes SD root')
        if not path.is_file():errors.append({'path':str(rel),'error':'missing'})
        elif path.stat().st_size!=row['size'] or digest(path)!=row['sha256']:errors.append({'path':str(rel),'error':'mismatch'})
    return errors

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    create=sub.add_parser('create');create.add_argument('--fonts',type=Path,required=True);create.add_argument('--proofs',type=Path,required=True);create.add_argument('--version',required=True);create.add_argument('--out',type=Path,required=True)
    check=sub.add_parser('verify');check.add_argument('root',type=Path);check.add_argument('manifest',type=Path)
    a=p.parse_args()
    if a.command=='verify':
        errors=verify(a.root,json.loads(a.manifest.read_text()));print(json.dumps({'result':'FAIL' if errors else 'PASS','errors':errors}));raise SystemExit(bool(errors))
    files={}
    for directory,pattern in [(a.fonts,'misans-*.pgf'),(a.proofs,'*.pfv2')]:
        for f in sorted(directory.glob(pattern)):
            if f.is_symlink():raise ValueError('Symlink resource')
            relative='paper/fonts/'+f.name
            if relative in files:raise ValueError('Duplicate resource')
            files[relative]={'path':relative,'size':f.stat().st_size,'sha256':digest(f)}
    if not files:raise ValueError('No resources')
    record={'schema':1,'version':a.version,'scope':'MiSans assets and proof sidecars; books and user records excluded','files':list(files.values())}
    with a.out.open('x') as f:json.dump(record,f,ensure_ascii=False,indent=2)
    print(json.dumps({'files':len(files),'manifest_sha256':digest(a.out)}))

if __name__=='__main__':main()
