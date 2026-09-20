"""FreeType -> A8 master -> exactly one final threshold. No CSS font rendering."""
from __future__ import annotations
from dataclasses import replace
from functools import lru_cache
from io import BytesIO
import hashlib, zlib
from PIL import Image
from .model import Spec, TEXTS, canonical, firmware_compatible
from .raster import Spec as RasterSpec, quantise, wrap, toolchain

COMPACT=(208,96)
WIDE=(432,224)

class Engine:
    def __init__(self,registry):self.registry=registry
    def clear(self):self.master.cache_clear()
    @lru_cache(maxsize=1200)
    def master(self,font,weight,px,algorithm,content,kind='compact'):
        spec=Spec(font=font,weight=weight,px=px,algorithm=algorithm,content=content)
        if algorithm=='FW':
            from .baseline import firmware_master
            return firmware_master(self.registry.repo, spec, kind)
        src=self.registry.get(font,weight)
        if getattr(src,'prebuilt',False):return src.sample(px,algorithm,content,kind)
        rs=RasterSpec(font,px,algorithm)
        width,height=WIDE if kind=='wide' else COMPACT
        canvas=Image.new('L',(width,height),0)
        if content=='body':
            if kind=='wide':
                lines=wrap(src,TEXTS[content][0],width-16,rs)
            else:lines=['清晨光线','慢慢移来']
        else:lines=list(TEXTS[content])
        missing=src.missing(''.join(lines))
        if missing:raise ValueError('样文缺字：'+''.join(missing))
        gs=[src.glyph(c,px,algorithm) for c in ''.join(lines)]
        asc=max((g.top for g in gs),default=px)
        desc=max((g.coverage.height-g.top for g in gs),default=0)
        step=max(px+2,asc+desc+2)
        baseline=4+asc
        visible=[];bounds=[]
        for line in lines:
            if baseline+desc>height-3:break
            x=8
            for char in line:
                g=src.glyph(char,px,algorithm)
                if g.coverage.width and g.coverage.height:
                    left,top=x+g.left,baseline-g.top
                    box=(left,top,left+g.coverage.width,top+g.coverage.height)
                    if min(left,top)<0 or box[2]>width or box[3]>height:raise ValueError('样本文字越界；保留真实字号，拒绝缩放')
                    canvas.paste(g.coverage,(left,top))
                    bounds.append(box)
                x+=g.advance
            if x>width:raise ValueError('样文超出行宽')
            visible.append(line);baseline+=step
        if not visible or (kind=='compact' and len(visible)!=len(lines)):
            raise ValueError('真实行高超过样本区域')
        raw=canvas.tobytes()
        return raw,{'width':width,'height':height,'source':src.info(),
          'visible_text':'\n'.join(visible),'text_sha256':hashlib.sha256('\n'.join(visible).encode()).hexdigest(),
          'master_sha256':hashlib.sha256(raw).hexdigest(),'master_crc32':zlib.crc32(raw),
          'requested_px':px,'resolved_px':px,'glyph_bounds':bounds,'kind':kind,
          'body_continues':content=='body' and len(visible)<len(lines)}
    def render(self,spec,kind='compact'):
        if spec.algorithm=='FW' and not firmware_compatible(spec):
            raise ValueError('固件原样固定：思源 Medium / 18、20、22、25、28、30 px / 2 bit / 黑白 / 阈值128；请点固件基准按钮')
        raw,meta=self.master(spec.font,spec.weight,spec.px,spec.algorithm,spec.content,kind)
        width,height=meta['width'],meta['height']
        from .gray import coverage_to_luma
        pixels=coverage_to_luma(raw,spec)
        image=Image.frombytes('L',(width,height),pixels)
        packed=image.convert('1',dither=Image.Dither.NONE).tobytes() if spec.output=='mono' else pixels
        signature={'spec':spec.data(),'source':meta['source']['sha256'],'master':meta['master_sha256'],'kind':kind}
        return image,meta|{'spec':spec.data(),'sample_id':hashlib.sha256(canonical(signature)).hexdigest()[:24],
           'resolved_ok':True,'fallback_used':False,'output_bpp':2 if spec.output=='gray4' else 1,'physical_quality':'NOT_PROVEN',
           'display_mode':spec.output,'gray_transfer':'uniform-v1' if spec.output=='gray4' else 'threshold',
           'region_crc32':zlib.crc32(image.tobytes()),'frame_sha256':hashlib.sha256(packed).hexdigest(),
           'threshold_effective':spec.output=='mono' and spec.algorithm not in ('M-A','M-N','FW'),
           'warning':'黑白栅格只有 0/255，墨色按钮不会改变结果。' if spec.algorithm in ('M-A','M-N') else ''}
    def png(self,spec,kind='compact'):
        im,meta=self.render(spec,kind);f=BytesIO();im.save(f,format='PNG');return f.getvalue(),meta
