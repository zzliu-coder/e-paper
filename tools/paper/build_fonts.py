#!/usr/bin/env python3
"""Build exact-size, 2bpp MiSans glyph resources from user-owned font files.
This tool never downloads or substitutes fonts. Generated font resources stay local.
"""
from __future__ import annotations
import argparse, hashlib, json, struct, zlib
from pathlib import Path
import freetype

def build_face(source:Path, output:Path, px:int, weight:int, chars:str, face_index:int=0, allow_test:bool=False, require_misans:bool=True, algorithm:str='N-A'):
    modes={'N-A':(freetype.FT_LOAD_TARGET_NORMAL,freetype.FT_RENDER_MODE_NORMAL),
           'M-A':(freetype.FT_LOAD_TARGET_MONO,freetype.FT_RENDER_MODE_MONO),
           'L-A':(freetype.FT_LOAD_TARGET_LIGHT,freetype.FT_RENDER_MODE_NORMAL)}
    if algorithm not in modes:raise ValueError('Unsupported raster algorithm')
    face=freetype.Face(str(source),index=face_index)
    family=face.family_name.decode('utf-8','replace') if face.family_name else ''
    if require_misans and not allow_test and 'misans' not in family.lower().replace(' ',''):
        raise ValueError(f'Expected MiSans; found {family!r}. No automatic substitution.')
    if not 16<=px<=40 or weight not in (400,500,700):raise ValueError('invalid size/weight')
    if not allow_test:
        from fontTools.ttLib import TTFont
        with TTFont(source,fontNumber=face_index) as tt:
            if 'fvar' in tt:
                axes=tt['fvar'].axes
                if not any(axis.axisTag=='wght' for axis in axes):raise ValueError('Variable font has no weight axis')
                coords=[weight if axis.axisTag=='wght' else axis.defaultValue for axis in axes]
                for axis,val in zip(axes,coords):
                    if not axis.minValue<=val<=axis.maxValue:raise ValueError('Requested weight exceeds variation range')
                face.set_var_design_coords(coords)
            elif int(tt['OS/2'].usWeightClass)!=weight:
                raise ValueError(f'Requested {weight}, source OS/2 weight is {tt["OS/2"].usWeightClass}')
    face.set_pixel_sizes(0,px)
    # Avoid embedded bitmap strikes and implicit renderer selection.
    target,render_mode=modes[algorithm]
    flags=freetype.FT_LOAD_NO_BITMAP|freetype.FT_LOAD_FORCE_AUTOHINT|target
    records=bytearray();data=bytearray();missing=[]
    for cp in sorted(set(map(ord,chars))):
        if not face.get_char_index(cp):missing.append(cp);continue
        face.load_char(cp,flags);face.glyph.render(render_mode)
        g=face.glyph;b=g.bitmap
        if b.width>96 or b.rows>96:raise ValueError(f'oversize glyph U+{cp:04X}')
        pixels=[];buffer=b.buffer
        for y in range(b.rows):
            row=y if b.pitch>=0 else b.rows-1-y
            for x in range(b.width):
                if b.pixel_mode==freetype.FT_PIXEL_MODE_MONO:
                    pixels.append(3 if buffer[row*abs(b.pitch)+x//8]&(128>>(x%8)) else 0)
                elif b.pixel_mode==freetype.FT_PIXEL_MODE_GRAY:
                    pixels.append(min(3,(buffer[row*abs(b.pitch)+x]+42)//85))
                else:raise ValueError('Unsupported FreeType bitmap format')
        packed=bytearray((len(pixels)+3)//4)
        for i,p in enumerate(pixels):packed[i//4]|=p<<(6-2*(i%4))
        records.extend(struct.pack('<IIIiHHhh',cp,len(data),len(packed),g.advance.x,b.width,b.rows,g.bitmap_left,g.bitmap_top));data.extend(packed)
    if missing and not allow_test:raise ValueError('font misses required glyphs: '+', '.join(f'U+{x:04X}'for x in missing[:20]))
    header=bytearray(80);header[:4]=b'PGF1'
    struct.pack_into('<HHHHIIIhh',header,4,1,px,weight,2,len(records)//24,80,80+len(records),face.size.ascender//64,face.size.descender//64)
    source_hash=hashlib.sha256(source.read_bytes()).digest();header[28:60]=source_hash
    struct.pack_into('<IIII',header,60,zlib.crc32(records),len(data),zlib.crc32(data),1|(0x80000000 if allow_test else 0))
    struct.pack_into('<I',header,76,zlib.crc32(header[:76]))
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(header+records+data)
    return dict(path=output.name,px=px,weight=weight,family=family,style=str(face.style_name),face_index=face_index,source_sha256=source_hash.hex(),sha256=hashlib.sha256(output.read_bytes()).hexdigest(),glyphs=len(records)//24,missing=len(missing),renderer='FreeType',freetype_version=freetype.version(),load_flags=flags,algorithm=algorithm,render_mode=render_mode,output_bpp=2,test_only=allow_test)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--regular',type=Path,required=True);p.add_argument('--medium',type=Path,required=True);p.add_argument('--bold',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--chars',type=Path);p.add_argument('--sizes',default='18,22,24,26,32');p.add_argument('--all-sizes',action='store_true');p.add_argument('--face-index',type=int,default=0);p.add_argument('--include-basic-cjk',action='store_true');a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    ui=''.join(f.read_text(encoding='utf-8') for f in (root/'components').rglob('*.cpp'))
    chars=''.join(chr(c)for c in range(32,127))+''.join(c for c in ui if ord(c)>127)
    if a.chars:chars+=a.chars.read_text(encoding='utf-8')
    if a.include_basic_cjk:chars+=''.join(chr(c)for c in range(0x4e00,0x9fff+1))
    sizes=list(range(16,41))if a.all_sizes else sorted(set(map(int,a.sizes.split(','))))
    entries=[]
    for weight,path in ((400,a.regular),(500,a.medium),(700,a.bold)):
        if path:
            for px in sizes:entries.append(build_face(path,a.output/f'misans-{weight}-{px}.pgf',px,weight,chars,a.face_index))
    (a.output/'manifest.json').write_text(json.dumps({'schema':1,'test_only':False,'files':entries},ensure_ascii=False,indent=2)+'\n')
    print(f'Generated {len(entries)} local font resources. Source font files were not copied.')
if __name__=='__main__':main()
