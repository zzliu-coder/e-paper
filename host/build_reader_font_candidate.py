"""Create a separate PFR candidate from local MiSans; never replaces installed fonts."""
import argparse
import hashlib
import importlib.util
import json
import struct
import zlib
from pathlib import Path
from fontTools.ttLib import TTFont


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--font',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--algorithm',choices=('M-A','N-A','L-A'),required=True)
    p.add_argument('--sizes',default='18,22,26,34')
    p.add_argument('--weights',default='400,500')
    a=p.parse_args()
    sizes=sorted(set(map(int,a.sizes.split(','))));weights=sorted(set(map(int,a.weights.split(','))))
    if not sizes or any(not 16<=x<=40 for x in sizes) or not weights or any(x not in (400,500,700) for x in weights):
        p.error('invalid size/weight')
    # Refuse reuse of an output directory: candidates and audit data are immutable.
    a.output.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).resolve().parents[1]/'tools/paper/build_fonts.py'
    spec=importlib.util.spec_from_file_location('paper_font_builder',source)
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    with TTFont(a.font) as f:cmap=f.getBestCmap()
    chars=''.join(chr(cp) for cp in sorted(cmap) if 32<=cp<=0x9fff or 0xff00<=cp<=0xffef)
    entries=[]
    for weight in weights:
        for size in sizes:
            path=a.output/f'{weight}-{size}.pgf'
            item=builder.build_face(a.font,path,size,weight,chars,algorithm=a.algorithm)
            item['bytes']=path.stat().st_size
            if item['bytes']>8*1024*1024:raise ValueError('Candidate face exceeds reader pack limit')
            entries.append(item);print(f'{a.algorithm} {weight}/{size}: {item["bytes"]}',flush=True)
    family='misans-'+a.algorithm.lower().replace('-','')
    label={'M-A':'MiSans 黑白对齐','N-A':'MiSans 标准对齐','L-A':'MiSans 轻微对齐'}[a.algorithm]
    manifest=f'PAPERFONT1 "{family}" "{label}" {len(entries)}\n'
    manifest+=''.join(f'{e["px"]} {e["weight"]} {e["bytes"]} {e["sha256"]}\n' for e in entries)
    manifest=manifest.encode();length=sum(e['bytes'] for e in entries);crc=0
    if length+len(manifest)+32>128*1024*1024:raise ValueError('Candidate pack exceeds installation limit')
    for e in entries:
        with (a.output/e['path']).open('rb') as f:
            while block:=f.read(65536):crc=zlib.crc32(block,crc)
    head=struct.pack('<4sHHIIII',b'PFR1',1,0,len(manifest),length,zlib.crc32(manifest),crc)
    head+=struct.pack('<II',zlib.crc32(head),0)
    pack=a.output/(family+'.pfr');digest=hashlib.sha256()
    with pack.open('xb') as out:
        for block in (head,manifest):out.write(block);digest.update(block)
        for e in entries:
            with (a.output/e['path']).open('rb') as f:
                while block:=f.read(65536):out.write(block);digest.update(block)
    receipt={'algorithm':a.algorithm,'family':family,'pack':pack.name,'sha256':digest.hexdigest(),
             'files':entries,'installed':False,'physical_quality':'NOT_PROVEN'}
    (a.output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'pack':str(pack),'sha256':digest.hexdigest()}))


if __name__=='__main__':main()
