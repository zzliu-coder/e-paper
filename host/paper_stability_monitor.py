"""Read-only duration-bounded device health sampling; never claims physical acceptance."""
import argparse
import json
import time
from pathlib import Path

def sample_health(query, log, seconds, interval=30, clock=time.monotonic, sleep=time.sleep, exercise=None, exercise_interval=300):
    """Injectable clock/transport; offline tests never import or open serial."""
    if not 0<seconds<=86400 or not 1<=interval<=60:raise ValueError('invalid sampling limits')
    started=clock(); count=0; first=None; last=None; heap_min=None; psram_min=None; max_request=0
    last_exercise=started;exercise_count=0
    def emit(row):log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
    try:
        initial=query('status');boot=initial['boot_id']
        while True:
            if exercise is not None and clock()-last_exercise>=exercise_interval:
                evidence=exercise();exercise_count+=1
                emit({'exercise':exercise_count,'elapsed_seconds':clock()-started,'evidence':evidence})
                last_exercise=clock()
            tick=clock();s=query('status');app=query('paper.status');elapsed=clock()-started
            row={'elapsed_seconds':elapsed,'request_seconds':clock()-tick,'status':s,
                 'app_error':app['app'].get('error'),'font_error':app['app'].get('font_error'),
                 'revision':app['app'].get('revision'),'presented':app['app'].get('presented')}
            emit(row);count+=1;first=first or s;last=s
            heap_min=min(heap_min if heap_min is not None else s['heap_free'],s['heap_free'])
            psram_min=min(psram_min if psram_min is not None else s['psram_free'],s['psram_free'])
            max_request=max(max_request,row['request_seconds'])
            if s['boot_id']!=boot or app['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
            if not s['board_ready'] or s.get('dropped',0):raise RuntimeError('Not ready or dropped packets')
            if row['app_error'] or row['font_error']:raise RuntimeError('Application or font error')
            if row['request_seconds']>30:raise RuntimeError('Health request exceeded 30 seconds')
            if elapsed>=seconds:break
            sleep(min(interval,seconds-elapsed))
        result={'result':'PASS','scope':'read-only health sampling; no physical or leak-free claim','seconds':elapsed,
                'boot_id':boot,'samples':count,'heap_min':heap_min,'psram_min':psram_min,
                'heap_start':first['heap_free'],'heap_end':last['heap_free'],'max_request_seconds':max_request,
                'exercise_count':exercise_count}
        if exercise is not None:result['scope']='health sampling plus verified next/previous page round trips; physical quality not inferred'
    except Exception as exc:
        emit({'result':'FAIL','seconds':clock()-started,'samples':count,'error':str(exc)})
        raise
    emit(result);return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--hours',type=float,default=8);p.add_argument('--interval',type=float,default=30);p.add_argument('--out',type=Path,required=True);p.add_argument('--exercise',action='store_true',help='Every 5 minutes turn next then previous and verify position/frame; requires an open book and no concurrent user input');a=p.parse_args()
    if not 0<a.hours<=24 or not 1<=a.interval<=60:p.error('hours 0..24; interval 1..60')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    from paper_action import query,action
    def roundtrip():
        before=query('paper.status')
        if before['app']['screen']!=2 or not before['app']['reader']['open']:raise RuntimeError('Reading exercise requires the original open book')
        forward=action('next',timeout=90);back=action('prev',timeout=90);after=back['receipt']
        if after['boot_id']!=before['boot_id'] or after['app']['frame_crc']!=before['app']['frame_crc']:raise RuntimeError('Round trip changed boot or original frame')
        for key in ('chapter','offset','page_hint','style'):
            if before['app']['reader'][key]!=after['app']['reader'][key]:raise RuntimeError('Round trip changed reader '+key)
        return {'next_seconds':forward['seconds'],'prev_seconds':back['seconds'],'boot_id':after['boot_id'],'frame_crc':after['app']['frame_crc']}
    with a.out.open('x') as log:
        print(json.dumps(sample_health(query,log,a.hours*3600,a.interval,exercise=roundtrip if a.exercise else None)))


if __name__=='__main__':main()
