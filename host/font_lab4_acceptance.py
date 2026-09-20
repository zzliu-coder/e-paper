from __future__ import annotations
if __name__ == '__main__':
    from pathlib import Path as _P
    import runpy as _run, sys as _sys
    _entry=_P(__file__).resolve().parents[1]/'tools/fontbench/app'/'device_acceptance.py'
    _sys.path.insert(0,str(_entry.parent))
    _run.run_path(str(_entry),run_name='__main__')
    raise SystemExit

#!/usr/bin/env python3
"""Explicit device pixel verification. Never rates fonts or changes firmware/partitions."""

import argparse,hashlib,json,time
from pathlib import Path
from PIL import Image

def app_status(reply):
    """Same nesting on normal, retry and timeout paths."""
    app=reply.get('app')
    if not isinstance(app,dict) or not isinstance(app.get('ui_demo'),dict):
        raise ValueError('Malformed inkdesk.status reply')
    return app

def verify_regions(image,samples):
    result=[]
    if image.size!=(480,800):raise ValueError('Wrong logical frame dimensions')
    gray=image.convert('L')
    for sample in samples:
        x,y,w,h=sample['box']
        if not (w>0 and h>0 and 0<=x<=480-w and 0<=y<=800-h):raise ValueError('Invalid ROI')
        actual=hashlib.sha256(gray.crop((x,y,x+w,y+h)).tobytes()).hexdigest()
        if actual!=sample['region_sha256']:
            raise AssertionError(f'Sample pixels differ: {sample["sample_id"]}')
        if sample['requested_px']!=sample['resolved_px'] or not sample['valid']:
            raise AssertionError(f'Unavailable/mis-sized sample: {sample["sample_id"]}')
        result.append({'sample_id':sample['sample_id'],'sha256':actual,'pixel_match':True})
    return result

def verify_manifest_page(lab,manifest):
    """Bind device-reported identities to the independently supplied build manifest."""
    if lab['build_id']!=manifest['build_id']:raise RuntimeError('Wrong SD bundle')
    pages=manifest['pages'];pid=lab['page_id']
    if not isinstance(pid,int) or not 0<=pid<len(pages):raise ValueError('Page outside manifest')
    expected=pages[pid]
    if expected['page_id']!=pid or expected['group']!=lab['group']:
        raise AssertionError('Wrong manifest page/group')
    if len(expected['samples'])!=len(lab['samples']):raise AssertionError('Wrong sample count')
    for actual,wanted in zip(lab['samples'],expected['samples']):
        for key in ('sample_id','requested_px','resolved_px','box','region_sha256'):
            if actual[key]!=wanted[key]:raise AssertionError(f'Manifest mismatch: {key}')
    return True

class Acceptance:
    def __init__(self,request,out,device_id=None,manifest=None):
        self.transport=request;self.out=out;self.device_id=device_id;self.boot=None;self.events=[];self.manifest=manifest
    def call(self,cmd,**args):
        reply=self.transport(cmd,args)
        if not isinstance(reply,dict) or reply.get('error') or not reply.get('ok'):
            raise RuntimeError(reply)
        if self.device_id and reply.get('device_id')!=self.device_id:raise RuntimeError('Unexpected device')
        if self.boot is None:self.boot=reply.get('boot_id')
        if reply.get('boot_id')!=self.boot:raise RuntimeError('Unexpected reboot')
        return reply
    def wait(self,predicate=lambda app:True,timeout=40):
        deadline=time.monotonic()+timeout;last=None
        while time.monotonic()<deadline:
            last=app_status(self.call('inkdesk.status'));ui=last['ui_demo']
            if last.get('refresh_error') or last.get('refresh_fault'):raise RuntimeError(last)
            if ui['visible'] and not ui['pending'] and ui['accepting_input'] and predicate(last):return last
            time.sleep(.2)
        raise TimeoutError(last)
    def tap_until(self,x,y,predicate):
        # Retry only after reading the actual resulting state. No blind double taps.
        for attempt in range(3):
            before=self.wait()
            if predicate(before):return before
            rev=before['ui_demo']['pixel_revision'];self.call('inkdesk.tap',x=x,y=y)
            try:return self.wait(lambda a:a['ui_demo']['pixel_revision']>rev and predicate(a))
            except TimeoutError:
                observed=app_status(self.call('inkdesk.status'))
                if predicate(observed) and observed['ui_demo']['accepting_input'] and not observed['ui_demo']['pending']:return observed
                if attempt==2:raise
        raise RuntimeError('Unreachable')
    def capture(self,name):
        for attempt in range(3):
            app=self.wait(lambda a: a['ui_demo']['page']==8 and not a['ui_demo']['fontlab4']['running'])
            u=app['ui_demo'];lab=u['fontlab4']
            if not lab['enabled'] or not lab['ready']:raise RuntimeError(lab)
            rev=u['pixel_revision'];data=bytearray()
            try:
                for offset in range(0,48000,512):
                    reply=self.call('inkdesk.ui.frame',offset=offset,length=min(512,48000-offset),revision=rev)
                    if reply['revision']!=rev:raise RuntimeError('stale_frame')
                    data.extend(bytes.fromhex(reply['hex']))
                if len(data)!=48000:raise ValueError('Bad frame size')
                after=app_status(self.call('inkdesk.status'))['ui_demo']
                if after['pixel_revision']!=rev:raise RuntimeError('stale_frame')
                image=Image.frombytes('1',(800,480),bytes(data)).transpose(Image.Transpose.ROTATE_270)
                checks=verify_regions(image,lab['samples'])
                if self.manifest:verify_manifest_page(lab,self.manifest)
                image.save(self.out/(name+'.png'))
                self.events.append({'capture':name,'page_id':lab['page_id'],'build_id':lab['build_id'],
                    'revision':rev,'sample_checks':checks,'last_full':lab['last_full']})
                return app
            except RuntimeError as e:
                if 'stale_frame' not in str(e) or attempt==2:raise
                time.sleep(.3)
        raise RuntimeError('Capture failed')
    def run(self):
        hello=self.call('hello')
        if hello.get('app_version')!='1.0.0-fontlab4':raise RuntimeError('Firmware version mismatch')
        self.call('inkdesk.ui.open')
        self.wait(lambda a:a['ui_demo']['page']==0)
        self.tap_until(380,710,lambda a:a['ui_demo']['page']==3)
        base=self.tap_until(80,340,lambda a:a['ui_demo']['page']==8)
        for group in range(6):
            self.tap_until(55+group*74,90,lambda a:g(a)==group)
            self.capture(f'group-{group+1}')
        # Explicitly capture the algorithm page's five actual sample regions, not changed buttons.
        self.tap_until(55,90,lambda a:g(a)==0)
        final=self.capture('algorithm-five-samples')
        revision=final['ui_demo']['pixel_revision']
        for _ in range(5):
            time.sleep(1)
            if self.wait()['ui_demo']['pixel_revision']!=revision:raise RuntimeError('Unexpected idle refresh')
        final=self.wait()
        if final['notes']!=base['notes'] or final['journal_sequence']!=base['journal_sequence']:
            raise RuntimeError('Unexpected document change')
        return {'result':'PASS','boot_id':self.boot,'events':self.events,
                'physical_quality':'NOT_PROVEN','feedback_write':'NOT_TESTED; no fabricated votes',
                'scope':'six groups, exact sample ROI pixels, size identity, boot and idle stability'}

def g(app):
    ui=app['ui_demo'];return ui.get('fontlab4',{}).get('group') if ui.get('page')==8 else None

def main(argv=None):
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--device-id');p.add_argument('--manifest',type=Path);a=p.parse_args(argv)
    if a.out.exists() and any(a.out.iterdir()):raise FileExistsError('Use a fresh evidence directory')
    a.out.mkdir(parents=True,exist_ok=True)
    from service import call,DEFAULT_SOCKET
    def request(cmd,args):
        r=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
        if not r.get('ok'):raise RuntimeError(r)
        return r['result']
    run=Acceptance(request,a.out,a.device_id,json.loads(a.manifest.read_text()) if a.manifest else None)
    try:result=run.run()
    except Exception as e:
        result={'result':'FAIL','error':str(e),'events':run.events}
        (a.out/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2));raise
    (a.out/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2));print(result['result'])
if __name__=='__main__':main()
