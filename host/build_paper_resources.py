#!/usr/bin/env python3
"""Build production PGF/IME resources locally. Never ships source fonts in git."""
import argparse,importlib.util,json
from pathlib import Path
from fontTools.ttLib import TTFont
from check_paper_ui_coverage import required_chars

def module(path,name):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def main():
    p=argparse.ArgumentParser();p.add_argument('--tools',type=Path,required=True);p.add_argument('--sources',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--ui-only',action='store_true');a=p.parse_args()
    builder=module(a.tools/'build_fonts.py','pgf_builder');dictionary=module(a.tools/'build_dictionary.py','ime_builder')
    root=Path(__file__).resolve().parents[1]
    required=required_chars(root)
    files=[]
    for weight,name in ((400,'Regular'),(500,'Medium'),(700,'Bold')):
        source=a.sources/'MiSansVF.ttf'
        with TTFont(source) as font:cmap=font.getBestCmap()
        # Full supported CJK + punctuation + ASCII; no unsupported codepoint fabrication.
        coverage={cp for cp in cmap if 32<=cp<=0x2fff or 0x3000<=cp<=0x9fff or 0xff00<=cp<=0xffef}
        missing=required-set(cmap)
        # Nonprinting controls are never emitted by the UI glyph renderer.
        missing-={0xfeff,0x200b}
        if missing:raise ValueError('Missing UI glyphs: '+repr(sorted(missing)))
        coverage|=required-set((0xfeff,0x200b))
        chars=''.join(map(chr,sorted(coverage)))
        for px in range(16,41):
            dest=a.output/'paper/fonts'/f'misans-{weight}-{px}.pgf'
            if not a.ui_only:
                entry=builder.build_face(source,dest,px,weight,chars)
                files.append(entry);print(f'{weight}/{px}: {entry["glyphs"]} glyphs',flush=True)
            ui_chars=''.join(map(chr,sorted(required-{0xfeff,0x200b})))
            files.append(builder.build_face(source,a.output/'paper/fonts'/f'misans-ui-{weight}-{px}.pgf',px,weight,ui_chars))
    (a.output/'paper/fonts'/('manifest-ui.json' if a.ui_only else 'manifest.json')).write_text(json.dumps({'schema':1,'test_only':False,'files':files},ensure_ascii=False,indent=2)+'\n')
    if a.ui_only:return
    ime=a.output/'paper/ime';dictionary.build(a.sources/'pinyin_simp.dict.yaml',ime)
    (ime/'LICENSE-source.txt').write_bytes((a.sources/'RIME-LICENSE').read_bytes())
    print('Production resources ready',flush=True)
if __name__=='__main__':main()
