from __future__ import annotations
if __name__ == '__main__':
    from pathlib import Path as _P
    import runpy as _run, sys as _sys
    _entry=_P(__file__).resolve().parents[1]/'tools/fontbench/app'/'open_device.py'
    _sys.path.insert(0,str(_entry.parent))
    _run.run_path(str(_entry),run_name='__main__')
    raise SystemExit

#!/usr/bin/env python3
"""Open a numbered prepared page through the existing USB service (RAM/UI only)."""
import argparse,time
from font_lab4_acceptance import app_status

def main():
    p=argparse.ArgumentParser();p.add_argument('--page',type=int,default=0);a=p.parse_args()
    if not 0<=a.page<4096:raise ValueError('Page must be 0..4095')
    from service import call,DEFAULT_SOCKET
    def request(cmd,**args):
        r=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
        if not r.get('ok') or r['result'].get('error'):raise RuntimeError(r)
        return r['result']
    request('inkdesk.ui.open')
    for _ in range(150):
        app=app_status(request('inkdesk.status'));ui=app['ui_demo']
        if ui['accepting_input'] and not ui['pending']:break
        time.sleep(.2)
    else:raise TimeoutError('UI not ready')
    request('inkdesk.fontlab4.page',page=a.page)
    for _ in range(150):
        app=app_status(request('inkdesk.status'));ui=app['ui_demo'];lab=ui.get('fontlab4',{})
        if ui['accepting_input'] and not ui['pending'] and ui['page']==8:
            if not lab.get('ready'):raise RuntimeError(lab)
            if lab['page_id']==a.page:print(f'Page {a.page}: {lab["build_id"]}');return
        time.sleep(.2)
    raise TimeoutError('Page was not rendered')
if __name__=='__main__':main()
