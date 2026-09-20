"""Use the existing Metalio single-owner service; never open a second serial port."""
from __future__ import annotations
import json,os,socket,time
from .model import Spec,ALGS,TEXTS
FONT_IDS=('sourcehan','misans','harmony','lxgw','wqy','pingfang','heiti','noto')
ALLOWED={'hello','inkdesk.status','inkdesk.fontbench.open','inkdesk.fontbench.control','inkdesk.fontbench.config','inkdesk.fontbench.state','inkdesk.fontbench.lock','inkdesk.fontlab.log','inkdesk.fontlab4.log','inkdesk.fontbench.log','inkdesk.ui.frame','inkdesk.fontbench.frame'}

def call(command,args=None,path=None):
    if command not in ALLOWED:raise ValueError('命令不在字体试验台允许列表')
    request={'cmd':command,'args':args or {}}
    raw=json.dumps(request).encode()+b'\n'
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
        s.settimeout(65);s.connect(path or f'/tmp/metalio-{os.getuid()}.sock');s.sendall(raw)
        data=bytearray()
        while len(data)<32768:
            b=s.recv(4096)
            if not b:break
            data.extend(b)
            if b'\n' in data:break
    if not data:raise ConnectionError('USB 服务没有响应')
    r=json.loads(bytes(data).split(b'\n')[0])
    if not r.get('ok'):raise RuntimeError(r.get('error',r))
    r=r.get('result',r)
    if r.get('error') or r.get('ok') is False:raise RuntimeError(r.get('error',r))
    return r

def state():return call('inkdesk.fontbench.state')
def wait(after=-1,boot=None,after_frame=-1):
    deadline=time.monotonic()+65
    while time.monotonic()<deadline:
        r=state();b=r.get('fontbench',{})
        if boot and r.get('boot_id')!=boot:raise RuntimeError('设备重启，已停止本次操作')
        if b.get('error'):raise RuntimeError(b['error'])
        if b.get('ready') and not b.get('pending') and b.get('revision',0)>after and b.get('frame_revision',0)>after_frame:return r
        time.sleep(.15)
    raise TimeoutError('墨水屏尚未完成本次刷新')

def control(field,value):
    before=state();rev=before.get('fontbench',{}).get('frame_revision',0)
    call('inkdesk.fontbench.control',{'field':field,'value':value})
    return wait(boot=before.get('boot_id'),after_frame=rev)

def apply(spec,axis,values=None,pin=None,refresh="full"):
    if refresh not in ("full","fast"):raise ValueError("刷新方式无效")
    spec=Spec.parse(spec)
    if type(axis) is not int or axis not in (0,1,2,3,10):raise ValueError('比较轴无效')
    hello=call('hello')
    if 'inkdesk.fontbench.config' not in hello.get('commands',[]):raise RuntimeError('请先安装统一版设备源码；当前固件缺少批量参数接口')
    field={0:'font',1:'px',2:'algorithm',3:'threshold',10:'output'}[axis]
    if values is None:
        values=[getattr(spec,field)] if axis==0 else [v for v in (spec.px-2,spec.px,spec.px+2,spec.px+4) if 16<=v<=40] if axis==1 else list(ALGS[:4]) if axis==2 else ['mono','gray4'] if axis==10 else [144,128,112]
    if not isinstance(values,list) or not 1<=len(values)<=4:raise ValueError('四格至少1项，最多4项')
    checked=[spec.with_value(field,v) for v in values]
    if len(set(map(str,values)))!=len(values):raise ValueError('比较值重复')
    encoded=[FONT_IDS.index(v) if axis==0 else ALGS.index(v) if axis==2 else ('mono','gray4').index(v) if axis==10 else v for v in values]
    payload={'spec':spec.data(),'axis':axis,'values':encoded,'refresh':refresh}
    if pin:
        ps=Spec.parse(pin['spec']);sha=pin.get('meta',{}).get('source',{}).get('sha256','')
        if ps.algorithm=='FW':
            device_sha=state().get('fontbench',{}).get('firmware_asset_sha','')
            if sha and sha!=device_sha:raise ValueError('电脑与真机的原 UI 字库身份不同；没有替换基准身份，也未发送参数')
            sha=device_sha
        if len(sha)!=64:raise ValueError('基准缺少源文件身份，不能发到设备冒充同一个基准')
        payload.update(pin_spec=ps.data(),pin_sha256=sha)
    before=state()['fontbench'].get('frame_revision',0)
    call('inkdesk.fontbench.config',payload)
    return wait(boot=hello.get('boot_id'),after_frame=before)

def logs():
    out={}
    for name,command in [('lab3','inkdesk.fontlab.log'),('lab4','inkdesk.fontlab4.log'),('fontbench','inkdesk.fontbench.log')]:
        data=bytearray();offset=0
        try:
            while offset<=262144:
                r=call(command,{'offset':offset});block=bytes.fromhex(r['hex'])
                data.extend(block);offset+=len(block)
                if not block:break
            out[name]={'text':data.decode('utf-8')}
        except Exception as e:out[name]={'error':str(e)}
    return out
