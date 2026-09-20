"""Portable A8 sample-image bank, explicitly not a font file or glyph library."""
from __future__ import annotations
import hashlib,json,os,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from .model import ALGS,RASTER_ALGS,SIZES,TEXTS,canonical,Spec
from .sources import CATALOG
from .raster import toolchain

MAGIC=b'FB5TILE\0'; HEADER=struct.Struct('<8s6I')
MAX_RAW=432*224

def pack(raw):
    out=bytearray();i=0
    while i<len(raw):
        j=i+1
        while j<len(raw) and raw[j]==raw[i] and j-i<130:j+=1
        if j-i>=3:
            out.extend((128+(j-i-3),raw[i]));i=j
        else:
            start=i;i=j
            while i<len(raw) and i-start<128:
                j=i+1
                while j<len(raw) and raw[j]==raw[i] and j-i<3:j+=1
                if j-i>=3:break
                i=min(j,start+128)
            out.append(i-start-1);out.extend(raw[start:i])
    return bytes(out)

def unpack(encoded,n):
    if type(n) is not int or not 0<n<=MAX_RAW:raise ValueError('Invalid raw budget')
    out=bytearray();i=0
    while i<len(encoded):
        ctrl=encoded[i];i+=1
        if ctrl&128:
            count=(ctrl&127)+3
            if i>=len(encoded):raise ValueError('Truncated RLE run')
            out.extend(bytes([encoded[i]])*count);i+=1
        else:
            count=ctrl+1
            if i+count>len(encoded):raise ValueError('Truncated RLE literal')
            out.extend(encoded[i:i+count]);i+=count
        if len(out)>n:raise ValueError('RLE budget exceeded')
    if len(out)!=n:raise ValueError('RLE size mismatch')
    return bytes(out)

def encode(raw,w,h,tag,kind):
    if w*h!=len(raw) or (w,h) not in ((208,96),(432,224)):raise ValueError('Wrong sample dimensions')
    body=pack(raw)
    return HEADER.pack(MAGIC,w,h,len(body),zlib.crc32(raw),tag,kind)+body

def decode(data,tag):
    if len(data)<HEADER.size:raise ValueError('Truncated tile')
    magic,w,h,n,crc,actual,kind=HEADER.unpack_from(data)
    if magic!=MAGIC or actual!=tag or kind not in (0,1) or (w,h)!=((208,96) if kind==0 else (432,224)):
        raise ValueError('Tile identity mismatch')
    if n!=len(data)-HEADER.size or n>2*w*h:raise ValueError('Tile length mismatch')
    raw=unpack(data[HEADER.size:],w*h)
    if zlib.crc32(raw)!=crc:raise ValueError('Tile CRC mismatch')
    return raw,(w,h),kind

def filename(font,weight,px,algorithm,content,kind=0):
    return f'{font}-{weight}-{px}-{algorithm}-{content}-{kind}.t5'

# Shared hit rectangles, all controls direct. No previous/next page controls.
def controls():
    out=[]
    def add(k,v,x,y,w,h,label):out.append(dict(field=k,value=v,box=[x,y,w,h],label=label))
    for i,c in enumerate(CATALOG):add('font',i,20+(i%4)*110,62+(i//4)*37,104,33,c['short'])
    for i,px in enumerate(SIZES):add('px',px,20+(i%9)*49,156+(i//9)*32,44,28,str(px))
    for i,label in enumerate(('自动黑白','自带黑白','自动标准','轻微对齐','自带标准','关闭微调','固件原样')):add('algorithm',i,20+(i%4)*110,270+(i//4)*34,104,30,label)
    for i,(v,label) in enumerate(((144,'淡'),(128,'标准'),(112,'浓'))):add('threshold',v,62+i*57,342,52,30,label)
    for i,(v,label) in enumerate(((400,'R'),(500,'M'),(700,'B'))):add('weight',v,316+i*48,342,42,30,label)
    for i,(v,label) in enumerate(((0,'比字体'),(1,'比字号'),(2,'比算法'),(3,'比墨色'),(10,'比输出'))):add('axis',v,20+i*89,377,83,29,label)
    for i,label in enumerate(('常用','密笔画','中英','正文')):add('content',i,20+i*82,410,76,29,label)
    add('inverse',0,353,410,50,29,'黑字');add('inverse',1,410,410,50,29,'白字')
    add('output',0,20,445,80,25,'黑白');add('output',1,108,445,80,25,'四灰阶')
    add('action',6,196,445,102,25,'启用灰阶');add('action',5,306,445,72,25,'色块');add('action',7,386,445,74,25,'恢复')
    for i,label in enumerate(('固定基准','取消固定','记为优选','全刷','快刷')):add('action',i,20+i*89,719,83,39,label)
    return out

def make_controls(font_path,face,out):
    font=lambda n:ImageFont.truetype(str(font_path),n,index=face)
    im=Image.new('L',(480,800),255);d=ImageDraw.Draw(im)
    d.text((20,8),'字体试验台',font=font(25),fill=0)
    d.text((330,17),'480×800 / 实验',font=font(15),fill=0)
    for xy,label in [((20,40),'字体'),((20,134),'字号 / px'),((20,248),'算法'),((20,348),'墨色'),((268,348),'字重')]:
        d.text(xy,label,font=font(16),fill=0)
    for c in controls():
        x,y,w,h=c['box'];d.rectangle((x,y,x+w-1,y+h-1),outline=0,width=1)
        f=font(17 if c['field'] not in ('font','algorithm','action') else 15)
        b=d.textbbox((0,0),c['label'],font=f)
        d.text((x+(w-(b[2]-b[0]))//2-b[0],y+(h-(b[3]-b[1]))//2-b[1]),c['label'],font=f,fill=0)
    for x,y in ((20,472),(248,472),(20,594),(248,594)):
        d.rectangle((x,y,x+211,y+121),outline=0)
    d.text((20,769),'点选样本，可固定一份基准持续比较',font=font(16),fill=0)
    im=im.point(lambda x:255 if x>=128 else 0).convert('1')
    Path(out).write_bytes(im.tobytes())
    return im

def build(engine,out,progress=None):
    out=Path(out)
    if out.exists() and any(out.iterdir()):raise FileExistsError('输出目录非空，请换一个新目录')
    out.mkdir(parents=True,exist_ok=True);(out/'tiles').mkdir()
    sources=engine.registry.public()
    if not any(x['available'] for x in sources):raise ValueError('没有可用的中文黑体')
    manifest={'schema':6,'catalog':sources,'sizes':list(SIZES),'algorithms':list(ALGS),
              'contents':list(TEXTS),'controls':controls(),'toolchain':toolchain(),
              'prebuilt_toolchains':[x.toolchain for x in engine.registry.faces.values() if getattr(x,'prebuilt',False)],
              'format':'A8 sample images; mono threshold or four-level luminance; physical gray is experimental','width':480,'height':800}
    tag=zlib.crc32(canonical(manifest));manifest['bundle_tag']=tag
    first=next((x for x in engine.registry.faces.values() if not getattr(x,'prebuilt',False)),None)
    if first:im=make_controls(first.path,first.face_index,out/'controls.i1')
    else:
        original=Path(engine.registry.prebuilt)/'controls.i1'
        (out/'controls.i1').write_bytes(original.read_bytes())
        im=Image.frombytes('1',(480,800),original.read_bytes())
    manifest['controls_crc32']=zlib.crc32((out/'controls.i1').read_bytes())
    hashes={};errors=[];done=0
    for fi,c in enumerate(sources):
        for weight in c['weights']:
            for px in SIZES:
                for ai,algorithm in enumerate(RASTER_ALGS):
                    for ti,content in enumerate(TEXTS):
                        for kind in (0,1) if content=='body' else (0,):
                            name=filename(fi,weight,px,ai,ti,kind)
                            try:
                                raw,meta=engine.master(c['id'],weight,px,algorithm,content,'wide' if kind else 'compact')
                                encoded=encode(raw,meta['width'],meta['height'],tag,kind)
                                (out/'tiles'/name).write_bytes(encoded)
                                hashes[name]=hashlib.sha256(encoded).hexdigest()
                            except ValueError as ex:errors.append({'tile':name,'error':str(ex)})
                            done+=1
                            if progress and done%25==0:progress(done)
    manifest['tile_count']=len(hashes);manifest['invalid_tiles']=errors
    (out/'manifest.json').write_bytes(canonical(manifest))
    (out/'tiles-sha256.json').write_bytes(canonical(hashes))
    if errors:raise ValueError(f'{len(errors)} 个样本生成失败，详情已记录；此目录不应直接部署')
    return manifest,im
