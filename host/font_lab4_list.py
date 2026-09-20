#!/usr/bin/env python3
"""Find exact sample pages without manually stepping through the complete experiment."""
import argparse,json
from pathlib import Path

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resources',type=Path,required=True)
    p.add_argument('--group',type=int,choices=range(1,7))
    p.add_argument('--px',type=int);p.add_argument('--font');p.add_argument('--algorithm')
    p.add_argument('--threshold',type=int);p.add_argument('--limit',type=int,default=30)
    a=p.parse_args(argv)
    if a.limit<1:raise ValueError('--limit must be positive')
    manifest=json.loads((a.resources/'manifest.json').read_text());rows=[]
    for page in manifest['pages']:
        if page['alternate'] or (a.group and page['group']!=a.group-1):continue
        samples=[s for s in page['samples']
                 if (a.px is None or s['requested_px']==a.px)
                 and (a.font is None or s['font_id']==a.font)
                 and (a.algorithm is None or s['algorithm']==a.algorithm)
                 and (a.threshold is None or s['threshold']==a.threshold)]
        if samples:rows.append((page,samples))
    print(f"build={manifest['build_id']} | matching pages={len(rows)}")
    for page,samples in rows[:a.limit]:
        print(f"{page['page_id']:04d} | group {page['group']+1} | {page['title']} | {page.get('subtitle','')}")
        print('      '+', '.join(s['sample_id'] for s in samples))
    if len(rows)>a.limit:print(f"Showing {a.limit}/{len(rows)}; narrow filters or increase --limit")
if __name__=='__main__':main()
