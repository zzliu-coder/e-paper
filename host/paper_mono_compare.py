"""Verify isolated software font alternatives; never enables panel grayscale."""
import argparse,json,statistics
from paper_action import action,query
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    initial=query('paper.status');boot=initial['boot_id'];settings=initial['app']['settings'];rows=[]
    with a.out.open('x') as log:
        def emit(x):log.write(json.dumps(x,ensure_ascii=False)+'\n');log.flush()
        emit({'initial':initial})
        def run(command,value=''):
            r=action(command,value,timeout=90,status_retries=1);s=r['receipt'];app=s['app'];emit({'command':command,'value':value,**r})
            assert s['boot_id']==boot and app['settings']==settings
            assert app['gray_test']['active'] and not app['gray_test']['gray']
            assert s['display']['success'] and s['display']['mode']=='mono'
            if command=='gray-test':assert app['mono_test_mode']==value
            print(command,value,app['gray_test']['page'],round(r['seconds'],3),flush=True)
            return r
        run('gray-test','mono')
        # Start from a known page without replaying unconfirmed commands.
        for _ in range(5):
            if query('paper.status')['app']['gray_test']['page']==0:break
            run('gray-test-next')
        assert query('paper.status')['app']['gray_test']['page']==0
        for page in range(5):
            for mode in ('mono','dots'):
                r=run('gray-test',mode);assert r['receipt']['app']['gray_test']['page']==page
                rows.append({'page':page,'mode':mode,'seconds':r['seconds'],'display_seconds':r['receipt']['display']['elapsed_us']/1e6})
            run('gray-test-next')
        run('gray-test','mono');run('gray-test-next') # leave 22px baseline for user
        emit({'result':'PASS','scope':'10 binary-driver combinations; visual acceptance pending',
              'samples':rows,'physical_quality':'NOT_PROVEN','reader_default_changed':False})
if __name__=='__main__':main()
