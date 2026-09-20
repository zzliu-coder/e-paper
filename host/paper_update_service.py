"""Install an already-staged SD package through the single-owner USB service.
Requires explicit version, SHA, current slot and device identity. No ROM writes.
"""
import argparse,json,time
from pathlib import Path
from service import call,DEFAULT_SOCKET

def main():
    p=argparse.ArgumentParser();p.add_argument('--version',required=True);p.add_argument('--sha256',required=True);p.add_argument('--running',type=lambda x:int(x,0),required=True);p.add_argument('--device-id',default='1020ba6e0be0');p.add_argument('--out',type=Path,required=True);p.add_argument('--install',action='store_true');a=p.parse_args()
    if not a.install:raise SystemExit('Explicit --install required')
    with a.out.open('x') as log:
        def query(cmd,**args):
            r=call(DEFAULT_SOCKET,dict(cmd=cmd,args=args));log.write(json.dumps(dict(time=time.time(),cmd=cmd,result=r),ensure_ascii=False)+'\n');log.flush()
            if not r.get('ok') or not r.get('result',{}).get('ok'):raise RuntimeError(r)
            if r['result'].get('device_id')!=a.device_id:raise RuntimeError('Device identity mismatch')
            return r['result']
        old=query('hello');state=query('paper.maintenance')['maintenance']
        if state['running_address']!=a.running or state['boot_address']!=a.running or state['usb_owned']:raise RuntimeError('Unsafe initial slot/USB state')
        if query('paper.file',op='hash',path='paper/updates/update.bin')['sha256']!=a.sha256:raise RuntimeError('Staged image mismatch')
        query('paper.command',action='maintenance-install')
        deadline=time.monotonic()+180
        while True:
            state=query('paper.maintenance')['maintenance'];phase=state['phase']
            if phase=='failed':raise RuntimeError(state)
            if phase=='ready-reboot':
                if a.version not in state['detail']:raise RuntimeError('Unexpected installed version')
                print('PASS inactive partition write and raw readback',flush=True);break
            if time.monotonic()>deadline:raise TimeoutError('Installation did not complete; no blind retry')
            time.sleep(1)
        query('paper.command',action='maintenance-reboot')
        deadline=time.monotonic()+150
        while time.monotonic()<deadline:
            try:
                hello=query('hello')
                if hello.get('app_version')==a.version and hello['boot_id']!=old['boot_id']:
                    state=query('paper.maintenance')['maintenance']
                    if state['running_address']==a.running:raise RuntimeError('Slot did not switch')
                    print('PASS new firmware boot',a.version,hex(state['running_address']),flush=True);return
            except (OSError,RuntimeError):pass
            time.sleep(1)
        raise TimeoutError('New boot not verified; inspect before any further write')
if __name__=='__main__':main()
