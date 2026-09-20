#!/usr/bin/env python3
"""Explicit experiment acceptance; never flashes, writes votes, or changes reader defaults.
Digital frame readback verifies a committed target buffer. Only a person can verify
physical grays, ghosting, contrast or reading comfort.
"""
from __future__ import annotations
from pathlib import Path
import argparse,json,time,zlib,dataclasses,hashlib
from PIL import Image
from fontbench import bridge
from fontbench.model import Spec,RASTER_ALGS,firmware_spec


def decode_frame(frame:bytes,bpp:int=2)->Image.Image:
    if bpp==1:
        if len(frame)!=48000:raise ValueError('I1 帧长度错误')
        native=Image.frombytes('1',(800,480),frame).convert('L')
    elif bpp==2:
        if len(frame)!=96000:raise ValueError('四灰阶目标帧长度错误')
        raw=bytearray(384000)
        for i,b in enumerate(frame):
            for k in range(4):raw[i*4+k]=((b>>(6-2*k))&3)*85
        native=Image.frombytes('L',(800,480),bytes(raw))
    else:raise ValueError('不支持的帧位深')
    return native.transpose(Image.Transpose.ROTATE_270)


def verify_card_pixels(frame,status,bpp=2):
    image=decode_frame(frame,bpp)
    for i,c in enumerate(status.get('cards',[])):
        calibration=status.get('calibration',False)
        if not c['resolved_ok'] and not calibration:continue
        if c['resolved_ok'] and (c['requested_px']!=c['resolved_px'] or c['fallback_used']):raise AssertionError('样本规格发生变化')
        # A locked gray request paints only the explicitly labelled BW fallback.
        if c['spec'].get('output')=='gray4' and not status.get('gray_armed'):continue
        x,y,w,h=c.get('box',[22+(i%2)*228,496+(i//2)*122,208,96])
        crop=image.crop((x,y,x+w,y+h));actual=zlib.crc32(crop.tobytes())
        if actual!=c['region_crc32']:raise AssertionError(f'样本 {i} 目标像素不一致: {actual} != {c["region_crc32"]}')
        if calibration and set(crop.tobytes())!={ [255,170,85,0][i] }:raise AssertionError('色块含网点或编码错误')
    return image


def capture():
    # USB frame reads are intentionally chunked; allow the service's cached
    # state snapshot to catch up without treating a stable frame as failure.
    for _ in range(20):
        b=bridge.wait()['fontbench'];rev=b['frame_revision'];data=bytearray()
        try:
            for offset in range(0,96000,512):
                r=bridge.call('inkdesk.fontbench.frame',{'offset':offset,'length':min(512,96000-offset),'revision':rev})
                if r['revision']!=rev:raise RuntimeError('stale_frame')
                if (r['width'],r['height'],r['bpp'])!=(800,480,2):raise RuntimeError('frame format mismatch')
                data.extend(bytes.fromhex(r['hex']))
            after=bridge.state()['fontbench']
            if after['frame_revision']!=rev:raise RuntimeError('stale_frame')
            return verify_card_pixels(bytes(data),b),b,bytes(data)
        except RuntimeError as e:
            if 'stale_frame' not in str(e):raise
            time.sleep(.25)
    raise RuntimeError('帧持续变化；请测试期间不要同时点屏幕')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--gray',action='store_true',help='显式执行面板灰阶；默认只测黑白')
    p.add_argument('--panel',choices=['GDEM0397T81'],help='灰阶测试需明确核对面板型号')
    p.add_argument('--exhaustive',action='store_true',help='在设备逐项验证25字号和6算法')
    a=p.parse_args()
    if a.gray and a.panel!='GDEM0397T81':p.error('灰阶需 --gray --panel GDEM0397T81；请先核对设备')
    if a.out.exists():raise FileExistsError('请使用新的证据目录')
    a.out.mkdir(parents=True);events=[];armed=False
    result={'schema':6,'result':'FAIL','physical_quality':'NOT_PROVEN','stage3_ready':False,
            'votes_written':False,'flash_written':False,'check_scope':'protocol + committed target buffers'}
    def remember(name):
        image,status,raw=capture();image.save(a.out/(name+'.png'))
        (a.out/(name+'.g2')).write_bytes(raw)
        events.append({'name':name,'state':status,'sha256':hashlib.sha256(raw).hexdigest()})
        return status
    try:
        hello=bridge.call('hello');boot=hello['boot_id']
        required={'inkdesk.fontbench.config','inkdesk.fontbench.frame','inkdesk.fontbench.lock'}
        if not required.issubset(hello.get('commands',[])):raise RuntimeError('请先安装阶段1/2源码；缺少新版接口')
        # Ignore phantom capacitive touches/keys during automated frame reads;
        # all requested changes still travel through the USB command path.
        bridge.call('inkdesk.fontbench.lock',{'enabled':True})
        before=bridge.state()['fontbench'].get('frame_revision',0)
        bridge.call('inkdesk.fontbench.open');state=bridge.wait(boot=boot,after_frame=before)['fontbench']
        # Select an explicitly resolved source, never silently a stand-in for one requested.
        good=next((c['spec'] for c in state.get('cards',[]) if c['resolved_ok'] and c['spec']['algorithm']!='FW'),None)
        if not good:raise RuntimeError('SD 没有可用字体样本；先生成并复制同版本资源')
        base=dataclasses.replace(Spec.parse(good),px=22,algorithm='N-A',threshold=128,input_bpp=8,output='mono',inverse=False)
        for alg in RASTER_ALGS:
            bridge.apply(dataclasses.replace(base,algorithm=alg).data(),2,[alg]);remember('mono-'+alg)
        sizes=list(range(16,41)) if a.exhaustive else [16,18,22,25,30,36,40]
        for px in sizes:
            algorithms=RASTER_ALGS if a.exhaustive else ['O-N','N-A']
            for alg in algorithms:
                bridge.apply(dataclasses.replace(base,px=px,algorithm=alg).data(),1,[px]);s=remember(f'size-{px}-{alg}')
                if any(not c['resolved_ok'] or c['resolved_px']!=px for c in s['cards']):raise AssertionError('精确字号未生效')
        # A baseline must be resolved on the device from its current real C font asset.
        original=[]
        for px in [18,20,22,25,28,30]:
            bridge.apply(firmware_spec(px).data(),1,[px]);s=remember('firmware-ui-'+str(px))
            original.append({'px':px,'resolved':all(c['resolved_ok'] and c['firmware_original'] for c in s['cards'])})
        result['original_ui']=original
        if not all(c['resolved'] for c in original):raise AssertionError('原固件基准有规格无法绘制，未用替补字体通过')
        if a.gray:
            bridge.apply(base.data(),10,['mono','gray4']);bridge.control('action',6);armed=True
            bridge.control('action',5);s=remember('gray-solid-blocks')
            if not s['calibration'] or s['display_mode']!='gray4-experimental' or not s['digital_commit']:raise AssertionError('真机未提交灰阶实验')
            bridge.apply(base.data(),10,['mono','gray4']);remember('same-master-mono-gray')
            for inverse in [True,False]:
                bridge.apply(dataclasses.replace(base,inverse=inverse).data(),10,['mono','gray4']);remember('polarity-'+str(int(inverse)))
            # Same final frame after explicit controlled fast cycles; receipts tell
            # us whether a requested fast refresh was promoted to a physical clean.
            for i in range(10):
                changing=dataclasses.replace(base,content='dense' if i%2==0 else 'common')
                bridge.apply(changing.data(),10,['mono','gray4'],refresh='fast');s=remember('fast-cycle-'+str(i+1))
                events[-1]['requested_refresh']='fast'
                if not s['digital_commit']:raise AssertionError('灰阶目标帧未提交')
            bridge.control('action',7);armed=False;s=remember('restored-mono')
            if s['gray_armed'] or s['display_mode']!='mono':raise AssertionError('未恢复黑白')
        before=bridge.state()['fontbench']['frame_revision'];time.sleep(3)
        if bridge.state()['fontbench']['frame_revision']!=before:raise AssertionError('空闲时反复刷新')
        if bridge.state()['boot_id']!=boot:raise RuntimeError('unexpected reboot')
        result.update(result='PASS',boot_id=boot,gray_executed=a.gray)
    except Exception as ex:
        result['error']=str(ex)
        if armed:
            try:result['recovery']=bridge.control('action',7)
            except Exception as recovery:result['recovery_error']=str(recovery)
    finally:
        try: bridge.call('inkdesk.fontbench.lock',{'enabled':False})
        except Exception as unlock_error: result['unlock_error']=str(unlock_error)
        result['events']=events;(a.out/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
    if result['result']!='PASS':raise SystemExit('验收未通过：'+result.get('error','unknown'))
    print('PASS：程序与目标帧校验通过。请在真机观察色阶、文字、残影；第三阶段仍等待人工选型。')
if __name__=='__main__':main()
