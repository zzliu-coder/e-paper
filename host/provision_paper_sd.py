#!/usr/bin/env python3
"""Install only manifested PAPER font/IME resources, verify each destination hash.
Existing unequal assets are retained in a timestamped on-card backup. No book deletion.
"""
import argparse,datetime,hashlib,json,shutil
from pathlib import Path
def digest(p):
    sha=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):sha.update(chunk)
    return sha.hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('volume',type=Path);p.add_argument('--receipt',type=Path,required=True);p.add_argument('--ui-and-ime',action='store_true',help='Install only UI subset and IME; full reading fonts are outside this receipt');p.add_argument('--ui-only',action='store_true',help='Install only UI subset, preserving all reading fonts and IME');a=p.parse_args()
    if a.volume.parent!=Path('/Volumes') or not a.volume.is_mount():raise ValueError('Expected an explicitly mounted /Volumes SD volume')
    rows=[]
    for rel in ('paper/fonts/manifest.json','paper/fonts/manifest-ui.json','paper/ime/manifest.json'):
        if a.ui_only and rel!='paper/fonts/manifest-ui.json':continue
        if a.ui_and_ime and rel=='paper/fonts/manifest.json':continue
        m=json.loads((a.source/rel).read_text())
        if m.get('test_only') is not False:raise ValueError('Test assets refused')
        base=Path(rel).parent
        for entry in m['files']:
            name=entry['path']
            if Path(name).name!=name:raise ValueError('Manifest path rejected')
            src=a.source/base/name
            if digest(src)!=entry['sha256']:raise ValueError('Source hash mismatch: '+name)
            rows.append((base/name,entry['sha256']))
        rows.append((Path(rel),digest(a.source/rel)))
    if not a.ui_only:rows.append((Path('paper/ime/LICENSE-source.txt'),digest(a.source/'paper/ime/LICENSE-source.txt')))
    total=sum((a.source/rel).stat().st_size for rel,_ in rows)
    if shutil.disk_usage(a.volume).free<total+16*1024*1024:raise ValueError('Insufficient free SD space')
    backup=a.volume/'.paper/resource-backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');receipts=[]
    for index,(rel,sha) in enumerate(rows):
        src=a.source/rel;dst=a.volume/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        if dst.exists() and digest(dst)==sha:receipts.append(dict(path=str(rel),sha256=sha,status='already-verified'));continue
        temp=dst.with_name(dst.name+'.provisioning')
        if temp.exists() and digest(temp)!=sha:
            old=backup/'incomplete'/rel;old.parent.mkdir(parents=True,exist_ok=True);temp.rename(old)
        if not temp.exists():shutil.copyfile(src,temp)
        if digest(temp)!=sha:raise ValueError('Destination readback mismatch')
        if dst.exists():old=backup/rel;old.parent.mkdir(parents=True,exist_ok=True);dst.rename(old)
        temp.rename(dst);receipts.append(dict(path=str(rel),sha256=sha,status='installed-verified'))
        if index%10==0:print(f'已校验 {index+1}/{len(rows)} 个资源文件',flush=True)
    a.receipt.parent.mkdir(parents=True,exist_ok=True);a.receipt.write_text(json.dumps(dict(volume=str(a.volume),bytes=total,files=receipts),indent=2)+'\n');print(f'Verified {len(rows)} files / {total} bytes. Safely eject on Mac next.')
if __name__=='__main__':main()
