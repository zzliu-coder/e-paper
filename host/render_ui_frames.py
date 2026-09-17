"""Offline layout check using device C++ drawing commands and existing fontpack.

This is a raster reference renderer, not an LVGL screenshot or e-ink acceptance.
The fontpack baseline/advance follows main/display/font/fontpack_lvgl.cc.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw
from build_ui_fonts import make_glyph, metrics

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('frames',type=Path)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--theme',type=int,choices=(0,1),default=0)
a=p.parse_args()
def glyph(c,size):
    try:return make_glyph(c,size,a.theme)
    except ValueError:return None
def ascent(size):
    height,baseline=metrics(size,a.theme)
    return height-baseline
issues=[]
a.out.mkdir(parents=True,exist_ok=True)
for line in a.frames.read_text().splitlines():
    f=json.loads(line);im=Image.new('L',(480,800),255);dr=ImageDraw.Draw(im)
    for d in f['draws']:
        x,y,w,h=d['box'];kind=d['kind'];color=0 if d['black'] else 255
        if not w or not h:continue
        rect=(x,y,x+w-1,y+h-1)
        if kind==0:
            size=next((n for n in (18,20,22,25,28,30) if d['size']<=n),30)
            mask=Image.new('L',(w,h));md=mask.load();cursor=0;base=ascent(size)
            for c in d['text']:
                g=glyph(c,size)
                if not g:
                    issues.append({'page':f['name'],'type':'missing_glyph','character':c,'size':size});continue
                gw,gh,gx,gy,advance,bits=g
                for j in range(gh):
                    for i in range(gw):
                        index=j*gw+i;opa=bits[index]
                        px=cursor+gx+i;py=base-gy-gh+j
                        if 0<=px<w and 0<=py<h:md[px,py]=opa
                cursor+=advance or (size//3 if c==' ' else 0)
            if cursor>w:issues.append({'page':f['name'],'type':'clipped_text','text':d['text'],'needed':cursor,'width':w})
            # Threshold matches a conservative monochrome preview; physical LUT differs.
            mask=mask.point(lambda v:255 if v>=128 else 0)
            im.paste(color,(x,y,x+w,y+h),mask)
        elif kind in (1,2,3):dr.rectangle(rect,fill=color if kind!=1 else None,outline=color)
        elif kind in (4,5):dr.rounded_rectangle(rect,d['radius'],fill=color if kind==5 else 255,outline=color,width=d['stroke'])
        elif kind==6:
            cell=d['size']
            for n,c in enumerate(d['text']):
                for row in range(7):
                    bits=d['dotRows'][n*7+row]
                    for col in range(5):
                        if bits&(1<<(4-col)):
                            px=x+n*6*cell+col*cell;py=y+row*cell
                            dr.rectangle((px,py,px+cell-2,py+cell-2),fill=color)
        elif kind in (7,8):
            matrix=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5));pixels=im.load()
            for yy in range(h):
                for xx in range(w):
                    if kind==7:ink=matrix[(yy+y)%4][(xx+x)%4]*100<d['density']*16
                    elif yy<d['stroke'] or yy>=h-d['stroke']:ink=xx%(d['dash']+d['gap'])<d['dash']
                    elif xx<d['stroke'] or xx>=w-d['stroke']:ink=yy%(d['dash']+d['gap'])<d['dash']
                    else:ink=False
                    pixels[x+xx,y+yy]=0 if ink else 255
    im.save(a.out/(f['name']+'.png'))
(a.out/'layout-check.json').write_text(json.dumps({'issues':issues,'renderer':'offline-fontpack-reference','physical_acceptance':'NOT_PROVEN'},ensure_ascii=False,indent=2))
print(json.dumps(issues,ensure_ascii=False,indent=2))
raise SystemExit(1 if issues else 0)
