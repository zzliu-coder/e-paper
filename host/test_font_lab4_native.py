#!/usr/bin/env python3
"""Compile the real parser/state machine with ASan/UBSan and the host cJSON library.
This does not compile ESP-IDF or claim to emulate e-paper optics.
"""
import argparse,ctypes.util,json,os,shutil,struct,subprocess,tempfile,zlib
from pathlib import Path
from fontlab4.packet import HEADER,decode,encode

def rewrite(data,change):
    meta,img=decode(data);change(meta);return encode(meta,img)

def main(argv=None):
    p=argparse.ArgumentParser();p.add_argument('--resources',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--no-sanitize',action='store_true')
    p.add_argument('--cjson-library',help='Explicit path or linker library name')
    a=p.parse_args(argv);root=Path(__file__).resolve().parents[1];a.out.mkdir(parents=True,exist_ok=True)
    raw=(a.resources/'0000.fl4').read_bytes();scratch=a.out/'scratch';scratch.mkdir(exist_ok=True)
    variants={}
    for pos in (0,8,12,16,20,24,28,32,-1):
        b=bytearray(raw);b[pos]^=1;variants[f'flip-{pos}']=bytes(b)
    for n in (0,1,31,32,100,len(raw)-1):variants[f'trunc-{n}']=raw[:n]
    variants['trailing']=raw+b'X'
    variants['bad-box']=rewrite(raw,lambda m:m['samples'][0].update(box=[0,0,480,800]))
    variants['bad-mode']=rewrite(raw,lambda m:m['samples'][0].update(algorithm='LCD'))
    variants['bad-schema']=rewrite(raw,lambda m:m.update(page_id=10))
    for name,data in variants.items():
        d=scratch/'invalid'/name;d.mkdir(parents=True,exist_ok=True);(d/'0000.fl4').write_bytes(data)
    d=scratch/'unavailable';d.mkdir(exist_ok=True)
    (d/'0000.fl4').write_bytes(rewrite(raw,lambda m:m['samples'][0].update(resolved_px=28,requested_px=25,resolved_ok=False)))
    lib=a.cjson_library or ctypes.util.find_library('cjson')
    if not lib:raise RuntimeError('Install a host cJSON library or pass --cjson-library; firmware uses ESP-IDF cJSON')
    link=[lib] if '/' in lib else ['-l:'+lib] if lib.endswith(('.so.1','.so')) else ['-lcjson']
    cxx=os.environ.get('CXX','c++');exe=a.out/'fontlab4-native'
    cmd=[cxx,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
         '-I'+str(root/'tests/fontlab4/stubs'),'-I'+str(root/'main'),
         str(root/'main/ui/fontlab4/core.cc'),str(root/'tests/fontlab4/native.cpp'),*link,'-o',str(exe)]
    if not a.no_sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    build=subprocess.run(cmd,text=True,capture_output=True)
    (a.out/'compiler.log').write_text(' '.join(cmd)+'\n'+build.stdout+build.stderr)
    build.check_returncode()
    run=subprocess.run([str(exe.resolve()),str(a.resources.resolve()),str(scratch.resolve())],text=True,capture_output=True)
    (a.out/'native.log').write_text(run.stdout+run.stderr);run.check_returncode()
    result=json.loads(run.stdout);result['negative_containers']=len(variants);result['sanitizers']=not a.no_sanitize
    result['hardware_tested']=False
    (a.out/'native.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
