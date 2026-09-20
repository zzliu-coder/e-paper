"""Small, versioned page container; frames are images, never executable assets."""
from __future__ import annotations
import json, struct, zlib, hashlib
from pathlib import Path
from PIL import Image

WIDTH, HEIGHT = 480, 800
FRAME_BYTES = WIDTH*HEIGHT//8
MAGIC = b'FLAB4P\0\0'
HEADER = struct.Struct('<8s6I')
MAX_META = 16384

def canonical(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'), sort_keys=True).encode('utf-8')

def region_bytes(image: Image.Image, box):
    x,y,w,h = box
    if not (w>0 and h>0 and x>=0 and y>=0 and x+w<=WIDTH and y+h<=HEIGHT):
        raise ValueError('Invalid sample rectangle')
    # 0 = black / 255 = white, one byte per pixel, independent of bit stride.
    return image.convert('L').crop((x,y,x+w,y+h)).tobytes()

def encode(meta, image):
    if image.size != (WIDTH,HEIGHT): raise ValueError('Wrong page dimensions')
    if not set(image.convert('L').tobytes()).issubset({0,255}):
        raise ValueError('I1 page contains unexpected gray; no implicit dithering')
    frame = image.convert('1',dither=Image.Dither.NONE).tobytes()
    meta = dict(meta)
    meta['schema']=4
    meta['width'],meta['height']=WIDTH,HEIGHT
    meta['output_bpp']=1
    meta['frame_sha256']=hashlib.sha256(frame).hexdigest()
    for s in meta['samples']:
        data=region_bytes(image,s['box'])
        s['region_crc32']=zlib.crc32(data)
        s['region_sha256']=hashlib.sha256(data).hexdigest()
    body=canonical(meta)
    if len(body)>MAX_META: raise ValueError('Page metadata budget exceeded')
    header=HEADER.pack(MAGIC,4,len(body),len(frame),zlib.crc32(body),zlib.crc32(frame),0)
    return header+body+frame

def decode(data):
    if len(data)<HEADER.size: raise ValueError('Truncated header')
    magic,ver,nmeta,nframe,mcrc,fcrc,reserved=HEADER.unpack_from(data)
    if magic!=MAGIC or ver!=4 or reserved!=0 or not 0<nmeta<=MAX_META or nframe!=FRAME_BYTES:
        raise ValueError('Unsupported page header')
    if len(data)!=HEADER.size+nmeta+nframe: raise ValueError('Truncated or trailing page bytes')
    raw=data[HEADER.size:HEADER.size+nmeta]; frame=data[HEADER.size+nmeta:]
    if zlib.crc32(raw)!=mcrc or zlib.crc32(frame)!=fcrc: raise ValueError('CRC mismatch')
    meta=json.loads(raw)
    if meta.get('schema')!=4 or meta.get('width')!=480 or meta.get('height')!=800 or meta.get('output_bpp')!=1:
        raise ValueError('Metadata identity mismatch')
    image=Image.frombytes('1',(WIDTH,HEIGHT),frame)
    for s in meta['samples']:
        rb=region_bytes(image,s['box'])
        if s['region_crc32']!=zlib.crc32(rb) or s['region_sha256']!=hashlib.sha256(rb).hexdigest():
            raise ValueError('Sample region mismatch')
    return meta,image
