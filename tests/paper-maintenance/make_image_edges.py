"""Synthetic bounded-image regressions; no modification of source books."""
from pathlib import Path
from PIL import Image, ImageDraw
import sys

out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for name,size,options in [
    ('odd.jpg',(1431,1051),{}),('boundary.jpg',(4096,4096),{}),
    ('reject-edge.jpg',(4097,800),{}),
    ('reject-progressive.jpg',(1431,1051),{'progressive':True}),
]:
    im=Image.new('RGB',size,'white');ImageDraw.Draw(im).rectangle((0,0,size[0]//2,size[1]),fill='black')
    with (out/name).open('xb') as f:im.save(f,format='JPEG',**options)
data=(out/'odd.jpg').read_bytes()
pos=2
while pos<len(data):
    marker=data[pos+1];length=int.from_bytes(data[pos+2:pos+4],'big')
    if marker in (0xc0,0xda):
        (out/f'reject-short-{marker:x}.jpg').write_bytes(data[:pos+2]+b'\x00\x03'+data[pos+4:pos+5]+data[pos+2+length:])
    if marker==0xc0:
        (out/'reject-duplicate-sof.jpg').write_bytes(data[:pos]+data[pos:pos+2+length]+data[pos:])
        (out/'reject-precision.jpg').write_bytes(data[:pos+4]+b'\x0c'+data[pos+5:])
    if marker==0xda:
        (out/'reject-short-dri.jpg').write_bytes(data[:pos]+b'\xff\xdd\x00\x03\x00'+data[pos:])
        break
    pos+=length+2
