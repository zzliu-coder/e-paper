"""Exercise native UI via the single-owner service; no audio/network/SD writes.
Screenshots are device framebuffer evidence, not physical e-ink photographs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time
from PIL import Image
from service import call, DEFAULT_SOCKET

p=argparse.ArgumentParser()
p.add_argument('--out',type=Path,required=True)
p.add_argument('--socket',default=DEFAULT_SOCKET)
p.add_argument('--version',default='1.0.0-inkdesk-ui3')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
events=[];boot=None;baseline=None
def request(cmd,**args):
    global boot
    response=call(a.socket,{'cmd':cmd,'args':args})
    if not response.get('ok'):raise RuntimeError(response)
    r=response['result']
    if r.get('error') or r.get('ok') is False:raise RuntimeError(r)
    if r.get('device_id')!='1020ba6e0be0':raise RuntimeError('device changed')
    if boot is None:boot=r['boot_id']
    if r['boot_id']!=boot:raise RuntimeError('device rebooted')
    if cmd!='inkdesk.ui.frame':events.append({'cmd':cmd,'args':args,'result':r})
    return r
def state():return request('inkdesk.status')['app']
def wait(page=None,after=None,after_pixel=None,**checks):
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        s=state();u=s['ui_demo']
        if s['refresh_error'] or s['refresh_fault']:raise RuntimeError('display refresh failure')
        if u['visible'] and not u['pending'] and u['accepting_input'] and (page is None or u['page']==page) and (after is None or u['revision']>after) and (after_pixel is None or u['pixel_revision']>after_pixel) and all(u[k]==v for k,v in checks.items()):return s
        time.sleep(.25)
    raise TimeoutError({'wanted':(page,checks),'last':s})
def tap(x,y,page=None,**checks):
    before=state()['ui_demo']['revision'];request('inkdesk.tap',x=x,y=y)
    return wait(page,after=before,**checks)
def capture(name):
    s=wait();revision=s['ui_demo']['pixel_revision'];assert revision>0
    buf=bytearray()
    for offset in range(0,48000,512):
        r=request('inkdesk.ui.frame',offset=offset,length=min(512,48000-offset),revision=revision)
        assert r['revision']==revision
        buf.extend(bytes.fromhex(r['hex']))
    (a.out/(name+'.bin')).write_bytes(buf)
    image=Image.frombytes('1',(800,480),bytes(buf))
    image.save(a.out/(name+'-native.png'))
    # EPD frame is native landscape; retain it and provide both orientation views.
    image.transpose(Image.Transpose.ROTATE_270).save(a.out/(name+'-portrait.png'))
    events.append({'capture':name,'revision':revision,'sha256':hashlib.sha256(buf).hexdigest()})
    print('Captured',name,flush=True)
    return image.transpose(Image.Transpose.ROTATE_270)
try:
    hello=request('hello');assert hello['app_version']==a.version
    old_pixel=state()['ui_demo']['pixel_revision']
    request('inkdesk.ui.open');baseline=wait(0,after_pixel=old_pixel)
    tap(380,710,3);tap(80,340,6)
    if state()['ui_demo']['theme']!='A-SourceHanSans-Medium':tap(100,250,6,theme='A-SourceHanSans-Medium')
    capture('fonts-A')
    tap(340,250,6,theme='B-LXGWWenKai-GB-Screen');capture('fonts-B')
    tap(220,640,0);capture('home-B')
    tap(380,710,3);tap(80,340,6);tap(100,250,6,theme='A-SourceHanSans-Medium')
    tap(220,640,0);capture('home-A')
    tap(60,560,1);tap(300,420,2)
    # Normalize RAM-only sample controls so the check can resume after a user try-out.
    if not state()['ui_demo']['controls']:tap(230,760,2,controls=True)
    while state()['ui_demo']['font']!=25:
        current=state()['ui_demo']['font'];tap(196 if current<25 else 50,650,2)
    if state()['ui_demo']['bookmark']:tap(340,650,2,bookmark=False)
    while state()['ui_demo']['reading_page']>1:tap(60,750,2)
    tap(385,750,2,controls=False)
    plain=capture('reading-A')
    anchor=state()['ui_demo']['reading_offset']
    tap(230,760,2,controls=True)
    controls=capture('reading-tools-A')
    assert plain.crop((24,130,456,560)).tobytes()==controls.crop((24,130,456,560)).tobytes()
    assert state()['ui_demo']['reading_offset']==anchor
    tap(196,650,2,font=28)
    tap(340,650,2,bookmark=True);tap(230,750,2,reading_page=2)
    capture('reading-controls-A');tap(385,730,2,controls=False)
    tap(60,30,0);tap(220,710,4);tap(300,300,4,wallpaper=1)
    tap(220,705,5);tap(60,30,0)
    # Hand off to the existing notes application; do not create/edit any note.
    request('inkdesk.tap',x=60,y=710)
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        s=state()
        if not s['ui_demo']['visible'] and s['view']==1:break
        time.sleep(.25)
    else:raise TimeoutError('notes handoff')
    assert s['notes']==baseline['notes'] and s['journal_sequence']==baseline['journal_sequence']
    old_pixel=state()['ui_demo']['pixel_revision']
    request('inkdesk.ui.open');wait(0,after_pixel=old_pixel)
    # Repeated A/B switches test actual font rendering and cleanup on the device.
    tap(380,710,3);tap(80,340,6)
    for i in range(6):
        theme='B-LXGWWenKai-GB-Screen' if i%2==0 else 'A-SourceHanSans-Medium'
        tap(340 if i%2==0 else 100,250,6,theme=theme)
    # Public component gallery; no real hardware settings or writes.
    tap(60,30,0);tap(380,710,3);tap(100,480,7,gallery=0)
    image=capture('gallery-text')
    for row in range(6):
        y=188+row*76
        # Matched text regions only, excluding unequal card widths/borders.
        left=image.crop((92,y,240,y+50));right=image.crop((290,y,438,y+50))
        assert all(l!=r for l,r in zip(left.getdata(),right.getdata())),('font complement',row)
    tap(188,90,7,gallery=1);capture('gallery-borders')
    tap(300,90,7,gallery=2);pattern=capture('gallery-gray')
    for row,density in enumerate((25,50,75)):
        patch=pattern.crop((132,198+row*132,440,274+row*132))
        values=list(patch.getdata());assert sum(v==0 for v in values)*100==len(values)*density
    tap(100,756,7,last_refresh='full');full=capture('gallery-gray-full')
    tap(340,756,7,last_refresh='fast');fast=capture('gallery-gray-fast')
    assert full.tobytes()==fast.tobytes()
    tap(405,90,7,gallery=3);capture('gallery-states')
    unchanged=state()['ui_demo']['revision'];request('inkdesk.tap',x=70,y=280);time.sleep(1)
    assert state()['ui_demo']['revision']==unchanged
    request('inkdesk.tap',x=300,y=280);time.sleep(1)
    assert state()['ui_demo']['revision']==unchanged
    tap(300,210,7,gallery_selected=True);tap(300,210,7,gallery_selected=False)
    # Leave the device on the text comparison and verify no idle redraws/reboots.
    tap(70,90,7,gallery=0)
    idle=state()['ui_demo']['pixel_revision']
    for _ in range(15):time.sleep(1);assert state()['ui_demo']['pixel_revision']==idle
    final=state();assert final['notes']==baseline['notes'] and not final['dirty']
    result={'result':'PASS','boot_id':boot,'baseline':baseline,'final':final,
            'physical_touch_and_font_preference':'NOT_PROVEN','events':events}
    print('PASS: device navigation, font symmetry, gallery states, spatial gray density, full/fast frame equality, stable reading body, notes preservation, idle no-redraw',flush=True)
except Exception as error:
    result={'result':'FAIL','error':str(error),'events':events}
    raise
finally:
    (a.out/'ui-acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
