"""Font laboratory device smoke test. No fabricated human feedback or book writes."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from PIL import Image
from service import call, DEFAULT_SOCKET

p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
events=[];boot=None
def request(cmd,**args):
    global boot
    response=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
    if not response.get('ok'):raise RuntimeError(response)
    r=response['result']
    if r.get('error') or not r.get('ok'):raise RuntimeError(r)
    assert r['device_id']=='1020ba6e0be0'
    if boot is None:boot=r['boot_id']
    assert r['boot_id']==boot,'unexpected reboot'
    if cmd!='inkdesk.ui.frame':events.append({'cmd':cmd,'args':args,'result':r})
    return r
def wait(page=None,after=None,after_pixel=None,**checks):
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        s=request('inkdesk.status')['app'];u=s['ui_demo']
        assert not s['refresh_error'] and not s['refresh_fault']
        if u['visible'] and not u['pending'] and u['accepting_input'] and (page is None or u['page']==page) and (after is None or u['revision']>after) and (after_pixel is None or u['pixel_revision']>after_pixel) and all(u[k]==v for k,v in checks.items()):return s
        time.sleep(.3)
    raise TimeoutError(s)
def tap(x,y,page=8,**checks):
    # The firmware reports the logical revision before the e-paper paint has
    # published its pixel revision.  Capture must wait for both; otherwise a
    # frame request can legitimately return stale_frame during a fast tap.
    last=None
    for _ in range(4):
        current=wait()['ui_demo']
        if checks and current['page']==page and all(current[k]==v for k,v in checks.items()):
            return {'ui_demo':current}
        before=current['revision'];before_pixel=current['pixel_revision']
        request('inkdesk.tap',x=x,y=y)
        try:
            return wait(page,after=before,after_pixel=before_pixel,**checks)
        except TimeoutError as exc:
            # A touch arriving while the e-paper frame is being committed is
            # deliberately rejected by the firmware.  Re-read the state before
            # retrying so an already-applied tap is never duplicated.
            observed=request('inkdesk.status')
            u=observed['ui_demo']
            if u['page']==page and all(u[k]==v for k,v in checks.items()):
                return observed
            last=exc
            time.sleep(.2)
    raise last or TimeoutError('tap was not accepted')
def capture(name):
    last=None
    for attempt in range(4):
        u=wait()['ui_demo'];rev=u['pixel_revision'];data=bytearray()
        try:
            for offset in range(0,48000,512):
                r=request('inkdesk.ui.frame',offset=offset,length=min(512,48000-offset),revision=rev)
                if r['revision']!=rev:raise RuntimeError('stale_frame')
                data.extend(bytes.fromhex(r['hex']))
            assert len(data)==48000
            (a.out/(name+'.bin')).write_bytes(data)
            Image.frombytes('1',(800,480),bytes(data)).transpose(Image.Transpose.ROTATE_270).save(a.out/(name+'.png'))
            events.append({'capture':name,'sha256':hashlib.sha256(data).hexdigest(),'revision':rev,'read_attempt':attempt+1})
            return
        except RuntimeError as exc:
            if 'stale_frame' not in str(exc):raise
            last=exc
            time.sleep(.5)
    raise last or RuntimeError('stale_frame')
def set_size_to(target):
    """Drive the size control to a target while tolerating an occasional ghost input.

    The device can receive a physical touch event while a frame is settling.  The
    acceptance test should verify the resulting state, not assume every tap was
    accepted exactly once.
    """
    for _ in range(12):
        u=wait()['ui_demo'];current=u['lab_size']
        if current==target:return
        tap(350 if current<target else 40,234)
    raise AssertionError(f'could not reach lab_size={target}, got {wait()["ui_demo"]["lab_size"]}')
try:
    assert request('hello')['app_version']=='1.0.0-fontlab3.1'
    old_pixel=request('inkdesk.status')['app']['ui_demo']['pixel_revision']
    request('inkdesk.ui.open');base=wait(0,after_pixel=old_pixel)
    tap(380,710,3);tap(80,340,8)
    assert wait()['ui_demo']['lab_page_count']==6
    # Establish a deterministic baseline for the captured page set.  The lab
    # intentionally keeps choices in RAM across re-entry, so an earlier manual
    # comparison must not change which host render is used for frame matching.
    tap(70,180,lab_profile=0)
    set_size_to(22)
    for page in range(6):
        tap(55+74*page,90,lab_page=page);capture('page-'+str(page))
    for profile,x in enumerate((70,180,290,400)):
        tap(x,180,lab_profile=profile);capture('profile-'+str(profile))
    set_size_to(40)
    set_size_to(16)
    set_size_to(22)
    tap(70,180,lab_profile=0);tap(150,90,lab_page=1)
    u=wait()['ui_demo'];assert u['last_refresh']=='full'
    for _ in range(8):
        time.sleep(1);now=wait()['ui_demo'];assert now['pixel_revision']==u['pixel_revision']
    final=wait();assert final['notes']==base['notes'] and final['journal_sequence']==base['journal_sequence']
    result={'result':'PASS','boot_id':boot,'final':final,'physical_font_quality':'NOT_PROVEN','sd_feedback_write':'NOT_PROVEN (no fabricated votes)','events':events}
    print('PASS: 6 pages, 4 static profiles, fontpack bpp2/bpp4 page, frame readback and idle stability; physical readability remains human review')
except Exception as exc:
    result={'result':'FAIL','error':str(exc),'events':events};raise
finally:
    (a.out/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
