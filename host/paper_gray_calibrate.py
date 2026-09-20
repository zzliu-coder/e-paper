"""Display one four-level calibration frame; never declares visual PASS."""
import argparse,json
from pathlib import Path
from paper_action import action,query

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--display-calibration',action='store_true')
    p.add_argument('--version',default='1.0.0-paper-grayab6')
    a=p.parse_args()
    if not a.display_calibration:p.error('Explicit --display-calibration required')
    with a.out.open('x') as log:
        def emit(row):log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
        try:
            hello=query('hello');initial=query('paper.status');emit({'hello':hello,'initial':initial})
            if hello['app_version']!=a.version:raise RuntimeError('Wrong calibration firmware')
            if hello['device_id']!='1020ba6e0be0':raise RuntimeError('Wrong device')
            r=action('gray-test','mono',timeout=90,status_retries=1);emit({'mono':r})
            # Normalize to block page while still using the B/W backend.
            for _ in range(5):
                s=query('paper.status')
                if s['app']['gray_test']['page']==4:break
                emit({'advance':action('gray-test-next',timeout=90,status_retries=1)})
            r=action('gray-test','gray4',timeout=90,status_retries=1);emit({'calibration':r})
            s=r['receipt'];d=s['display'];test=s['app']['gray_test']
            assert s['boot_id']==initial['boot_id'] and s['app']['settings']==initial['app']['settings']
            assert test['gray'] and test['page']==4 and d['success'] and d['mode']=='gray4-experimental'
            assert [x['sequence'] for x in d['phases']]==[0xFC,0xFC,0xCC,0x83]
            assert all(x['complete'] for x in d['phases'])
            for _ in range(3):
                status=query('paper.status');emit({'heartbeat':status})
                assert status['boot_id']==initial['boot_id']
            emit({'digital_result':'PASS','physical_quality':'NOT_PROVEN','left_at':'true-gray four blocks','default_changed':False})
            print('PASS digital calibration; inspect clean white background and four distinct levels. Physical quality NOT_PROVEN.')
        except Exception as e:
            emit({'digital_result':'FAIL','error':str(e),'recovery':'No automatic reset, reflash or repeated gray command'})
            raise
if __name__=='__main__':main()
