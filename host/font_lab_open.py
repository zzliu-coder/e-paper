from __future__ import annotations
if __name__ == '__main__':
    from pathlib import Path as _P
    import runpy as _run, sys as _sys
    _entry=_P(__file__).resolve().parents[1]/'tools/fontbench/app'/'open_device.py'
    _sys.path.insert(0,str(_entry.parent))
    _run.run_path(str(_entry),run_name='__main__')
    raise SystemExit

"""Open the existing on-device lab through the resident USB service."""
import json
import time
from service import call, DEFAULT_SOCKET

def request(cmd,**args):
    response=call(DEFAULT_SOCKET,dict(cmd=cmd,args=args))
    # Board-ready handshake can precede the application's first snapshot.
    if cmd=='inkdesk.status':
        deadline=time.monotonic()+25
        while response.get('error')=='app_starting' and time.monotonic()<deadline:
            time.sleep(.3);response=call(DEFAULT_SOCKET,dict(cmd=cmd,args=args))
    if not response.get('ok'):raise RuntimeError(response)
    r=response['result']
    if r.get('error') or not r.get('ok'):raise RuntimeError(r)
    assert r['device_id']=='1020ba6e0be0'
    return r

def wait(page,after=None,pixel=None):
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        r=request('inkdesk.status');u=r['app']['ui_demo']
        if u['page']==page and u['visible'] and u['accepting_input'] and not u['pending'] and (after is None or u['revision']>after) and (pixel is None or u['pixel_revision']>pixel):return u
        time.sleep(.3)
    raise TimeoutError(r)

if __name__=='__main__':
    assert request('hello')['app_version']=='1.0.0-fontlab3.1'
    current=request('inkdesk.status')['app']['ui_demo']
    request('inkdesk.ui.open');current=wait(0,pixel=current['pixel_revision'])
    for x,y,page in ((380,710,3),(80,340,8),(150,90,8)):
        request('inkdesk.tap',x=x,y=y);current=wait(page,after=current['revision'])
    print(json.dumps(current,ensure_ascii=False))
