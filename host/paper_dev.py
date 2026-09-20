#!/usr/bin/env python3
"""PAPER's local developer client: diagnostics, safe file upload, update packaging.
Firmware installation is explicit; it only uses the device's checked inactive-slot updater.
"""
import argparse,hashlib,json,shutil,struct,time
from pathlib import Path
from metalio import Client,Journal,open_serial,choose_port

def package(image,out):
    data=image.read_bytes()
    if len(data)<288 or len(data)>5*1024*1024 or data[0]!=0xe9 or struct.unpack_from('<H',data,12)[0]!=9:
        raise ValueError('Requires an ESP32-S3 app image <=5MiB, never a merged full-flash image')
    if struct.unpack_from('<I',data,32)[0]!=0xabcd5432:raise ValueError('Missing app descriptor')
    version=data[48:80].split(b'\0')[0].decode();project=data[80:112].split(b'\0')[0].decode()
    if project!='xiaozhi' or 'paper' not in version:raise ValueError('Requires PAPER development firmware')
    out.mkdir(parents=True,exist_ok=False)
    shutil.copyfile(image,out/'update.bin')
    manifest=dict(schema=1,board='metalio_eink4',layout='metalio-16m-v1-5m',version=version,size=len(data),sha256=hashlib.sha256(data).hexdigest())
    (out/'update.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--device-id',default='1020ba6e0be0');p.add_argument('--log',type=Path)
    sub=p.add_subparsers(dest='op',required=True)
    s=sub.add_parser('package');s.add_argument('image',type=Path);s.add_argument('--out',type=Path,required=True)
    sub.add_parser('status')
    s=sub.add_parser('action');s.add_argument('action',choices=['maintenance','home','settings','library','maintenance-awake','maintenance-resources','maintenance-usb','maintenance-check','maintenance-install','maintenance-reboot']);s.add_argument('--timeout',type=int,default=300)
    s=sub.add_parser('put');s.add_argument('source',type=Path);s.add_argument('destination')
    a=p.parse_args()
    if a.op=='package':print(json.dumps(package(a.image,a.out),indent=2));return
    journal=Journal(a.log);link=open_serial(choose_port(a.port));client=Client(link,journal,a.device_id)
    try:
        client.hello()
        deadline=time.monotonic()+45
        while not client.request('status').get('board_ready'):
            if time.monotonic()>deadline:raise TimeoutError('Board did not initialize')
            time.sleep(.25)
        if a.op=='status':
            print(json.dumps(client.request('paper.status'),ensure_ascii=False,indent=2));print(json.dumps(client.request('paper.maintenance'),ensure_ascii=False,indent=2))
        elif a.op=='action':
            before=client.request('paper.status')['app'].get('revision',0)
            receipt=client.request('paper.command',action=a.action);print(json.dumps(receipt),flush=True)
            if a.action in ('maintenance-usb','maintenance-reboot'):return
            deadline=time.monotonic()+a.timeout
            while time.monotonic()<deadline:
                s=client.request('paper.status')
                if s.get('last_action')==a.action and s.get('app',{}).get('revision',0)>before:
                    print(json.dumps(s,ensure_ascii=False,indent=2))
                    if s.get('last_action_error'):raise RuntimeError(s['last_action_error'])
                    return
                time.sleep(.5)
            raise TimeoutError('Queued operation not confirmed complete')
        elif a.op=='put':
            length=a.source.stat().st_size
            h=hashlib.sha256()
            with a.source.open('rb') as f:
                for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
            sha=h.hexdigest()
            tid=client.request('paper.transfer',op='begin',path=a.destination,size=length,sha256=sha)['transfer_id']
            try:
                with a.source.open('rb') as f:
                    off=0
                    while chunk:=f.read(512):client.request('paper.transfer',op='chunk',transfer_id=tid,offset=off,hex=chunk.hex());off+=len(chunk)
                print(json.dumps(client.request('paper.transfer',op='commit',transfer_id=tid),indent=2))
            except Exception:
                # No blind retry of writes; the device retains unconfirmed transfers.
                raise
    finally:link.close();journal.close()
if __name__=='__main__':main()
