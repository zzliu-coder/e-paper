#!/usr/bin/env python3
"""Convert a licensed user-supplied Rime TSV/YAML dictionary into bounded disk indices."""
from __future__ import annotations
import argparse, hashlib, json, re, struct, zlib
from pathlib import Path
KEYPAD=dict(zip('abcdefghijklmnopqrstuvwxyz','22233344455566677778889999'))

def entries(source:Path):
    result={};invalid=0
    for line in source.read_text(encoding='utf-8-sig').splitlines():
        if not line or line.startswith('#') or '\t'not in line:continue
        fields=line.split('\t');word,pinyin=fields[:2]
        key=pinyin.lower().replace('ü','v').replace(' ','').replace("'",'')
        if not word or len(word.encode())>192 or not re.fullmatch('[a-z]{1,47}',key):invalid+=1;continue
        raw=fields[2].strip().rstrip('%') if len(fields)>2 else '1'
        try:freq=min(1000000,max(1,int(float(raw))))
        except ValueError:freq=1
        ident=(key,word);result[ident]=max(result.get(ident,0),freq)
    if not result:raise ValueError('No usable word/pinyin records')
    if len(result)>500000:raise ValueError('Dictionary exceeds the 500000-entry limit')
    return [(key,word,freq) for (key,word),freq in result.items()],invalid

def build(source:Path,out:Path,test=False):
    rows,invalid=entries(source)
    if not test and len(rows)<5000:raise ValueError('Production dictionary needs >=5000 entries; tiny fixtures are for tests only')
    out.mkdir(parents=True,exist_ok=True);files=[]
    for nine in (False,True):
        records=bytearray();data=bytearray()
        ordered=sorted(rows,key=lambda x:(''.join(KEYPAD[c]for c in x[0])if nine else x[0],-x[2],x[1]))
        for py,word,freq in ordered:
            key=''.join(KEYPAD[c]for c in py)if nine else py
            w,p=word.encode(),py.encode();records+=struct.pack('<48sIHHII',key.encode(),len(data),len(w),len(p),freq,0);data+=w+p
        h=bytearray(32);h[:4]=b'PIM1';struct.pack_into('<IIIIII',h,4,1|(0x80000000 if test else 0),len(rows),64,32+len(records),len(data),zlib.crc32(records));struct.pack_into('<I',h,28,zlib.crc32(h[:28]));name='nine.pim'if nine else'pinyin.pim';(out/name).write_bytes(h+records+data);files.append({'path':name,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()})
    manifest={'schema':1,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'entries':len(rows),'ignored_rows':invalid,'test_only':test,'files':files};(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n');return manifest
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--license',required=True,type=Path);a=p.parse_args();m=build(a.source,a.output);(a.output/'LICENSE-source.txt').write_bytes(a.license.read_bytes());print(json.dumps(m,ensure_ascii=False))
