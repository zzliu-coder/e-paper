"""Explicit isolated mono/gray same-page comparison. No flash or reader-style writes."""
import argparse,json,statistics,time
from pathlib import Path
from paper_action import action,query

def summarize(rows):
    result={}
    for mode in ('mono','gray4'):
        samples=[r for r in rows if r['kind']=='steady' and r['mode']==mode]
        if not samples:raise ValueError('No measured samples')
        wall=sorted(r['seconds'] for r in samples)
        result[mode]={'samples':len(wall),'mean_seconds':statistics.mean(wall),
          'median_seconds':statistics.median(wall),'min_seconds':min(wall),'max_seconds':max(wall),
          'display_mean_seconds':statistics.mean(r['receipt']['display']['elapsed_us']/1e6 for r in samples),
          'phase_counts':sorted(set(len(r['receipt']['display'].get('phases',[])) for r in samples)) if mode=='gray4' else None}
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--execute-gray',action='store_true');p.add_argument('--cycles',type=int,default=6)
    p.add_argument('--visual-calibration-confirmed',action='store_true',help='Use only after the user confirms clean four-level blocks on this build')
    args=p.parse_args()
    if not args.execute_gray:p.error('Requires --execute-gray')
    if not args.visual_calibration_confirmed:p.error('Run block calibration and obtain physical confirmation before repeated gray text tests')
    if args.cycles<2 or args.cycles>10 or args.cycles%2:p.error('cycles must be even, 2..10')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    rows=[];frames={};initial=query('paper.status');boot=initial['boot_id'];settings=initial['app']['settings']
    with args.out.open('x') as log:
        def emit(row):log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
        emit({'initial':initial,'physical_quality':'NOT_PROVEN'})
        def run(name,value,kind,mode):
            row=action(name,value,timeout=90);s=row['receipt'];test=s['app'].get('gray_test',{})
            emit({'kind':kind,'mode':mode,**row})
            if s['boot_id']!=boot or not test.get('active') or test.get('gray')!=(mode=='gray4'):raise RuntimeError('Wrong device or test mode')
            if s['app']['settings']!=settings:raise RuntimeError('Reader/system settings changed')
            display=s.get('display',{})
            if not display.get('success'):raise RuntimeError('No successful display receipt')
            if display.get('mode')!=('gray4-experimental' if mode=='gray4' else 'mono'):raise RuntimeError('Wrong display backend')
            if mode=='gray4' and (not display.get('phases') or not all(x['complete'] for x in display['phases'])):raise RuntimeError('Incomplete gray phase')
            key=(mode,test['page']);crc=test['frame_crc']
            if key in frames and frames[key]!=crc:raise RuntimeError('Same test page pixels changed')
            frames[key]=crc;rows.append({'kind':kind,'mode':mode,**row})
            print(kind,mode,test['page'],round(row['seconds'],3),flush=True)
        error=None
        try:
            # Warm both font paths before measured ABBA blocks.
            run('gray-test','mono','warmup','mono');run('gray-test','gray4','warmup','gray4')
            for mode in ('mono','gray4','gray4','mono'):
                run('gray-test',mode,'transition',mode)
                for _ in range(args.cycles):run('gray-test-next','','steady',mode)
        except Exception as e:
            error=e;emit({'result':'FAIL','error':str(e)})
        finally:
            # Leave an explicit mono test page with manual A/B buttons. Do not
            # change stored styles or issue a reset on failed panel recovery.
            try:run('gray-test','mono','restore','mono')
            except Exception as e:emit({'recovery_error':str(e)});error=error or e
        if error:raise error
        emit({'result':'PASS','scope':'digital receipts and timings only','summary':summarize(rows),
              'physical_clarity':'NOT_PROVEN','physical_black_flash':'NOT_PROVEN','default_changed':False})
if __name__=='__main__':main()
