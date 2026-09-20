"""Explicit real-device reading regression, using a fixed library index/TOC.

Changes the visible book/page, saves normal reading position, never flashes.
Run without concurrent physical input. Each action verifies its unique receipt.
"""
import argparse,json,statistics
from pathlib import Path
from paper_action import query,action

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--version',required=True);p.add_argument('--book',required=True);p.add_argument('--toc',required=True)
    p.add_argument('--pairs',type=int,default=10);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if not 1<=a.pairs<=50:p.error('pairs must be 1..50')
    with a.out.open('x') as log:
        def save(name,r):
            log.write(json.dumps({'check':name,'result':r},ensure_ascii=False)+'\n');log.flush()
            return r
        def act(name,value=''):
            r=save(name,action(name,value,120));print(name,round(r['seconds'],3),flush=True);return r
        hello=save('hello',query('hello'));assert hello['app_version']==a.version
        if not query('paper.maintenance')['maintenance']['awake']:act('maintenance-awake')
        act('library');act('open',a.book);act('toc-go',a.toc)
        original=query('paper.status')
        act('settings-category','display');returned=act('continue')['receipt']
        def same_page(s):
            assert s['boot_id']==original['boot_id']
            assert s['app']['frame_crc']==original['app']['frame_crc']
            for key in ('chapter','offset','page_hint','style'):
                assert s['app']['reader'][key]==original['app']['reader'][key],key
        same_page(returned)
        act('next');same_page(act('prev')['receipt'])
        baseline=query('paper.status');times=[]
        for n in range(a.pairs):
            times.append(act('next')['seconds']);back=act('prev');times.append(back['seconds']);same_page(back['receipt'])
            cache=back['receipt']['app']['font_cache'];old=baseline['app']['font_cache']
            assert cache['validations']==old['validations'],'warm page triggered font validation'
            assert cache['index_loads']==old['index_loads'],'warm page reloaded index'
            assert cache['active_evictions']==old['active_evictions'],'active font evicted'
            assert back['receipt']['dropped']==baseline['dropped']==0,'transport loss'
        act('library');same_page(act('open',a.book)['receipt'])
        result={'result':'PASS','scope':'same page, settings return, warm next/prev, library reselection; actual close/reopen, reboot and physical appearance separate',
                'pairs':a.pairs,'median_seconds':statistics.median(times),'p95_seconds':sorted(times)[min(len(times)-1,int(len(times)*.95))],
                'max_seconds':max(times),'boot_id':hello['boot_id']}
        save('summary',result);print(json.dumps(result),flush=True)
if __name__=='__main__':main()
