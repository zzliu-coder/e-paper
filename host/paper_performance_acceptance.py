"""Measure acknowledged PAPER operations through the existing single-owner service.
No flashing, credential handling or microphone capture. Uses the existing first book.
"""
import argparse,json,time,statistics
from pathlib import Path
from service import call,DEFAULT_SOCKET

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True);parser.add_argument('--network',action='store_true');parser.add_argument('--rounds',type=int,default=3);args=parser.parse_args()
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('x') as log:
        def query(cmd,**kwargs):
            reply=call(DEFAULT_SOCKET,dict(cmd=cmd,args=kwargs))
            if not reply.get('ok') or not reply.get('result',{}).get('ok'):raise RuntimeError(reply)
            return reply['result']
        initial=query('hello');boot=initial['boot_id'];samples=[]
        if 'revision' not in query('paper.status').get('app',{}):
            query('paper.open')
            deadline=time.monotonic()+90
            while 'revision' not in query('paper.status').get('app',{}):
                if time.monotonic()>deadline:raise TimeoutError('paper.open')
                time.sleep(.5)
        log.write(json.dumps({'baseline':initial},ensure_ascii=False)+'\n');log.flush()
        def action(name,value=''):
            prior=query('paper.status');before=prior['app']['revision'];start=time.monotonic();query('paper.command',action=name,value=value)
            while time.monotonic()-start<120:
                state=query('paper.status');app=state['app']
                if state['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
                if app['revision']>before and state.get('last_action')==name and app['presented']==app['revision'] and state.get('performance',{}).get('completed',0)>prior.get('performance',{}).get('completed',0):
                    entry=dict(action=name,value=value,seconds=round(time.monotonic()-start,3),state=state)
                    log.write(json.dumps(entry,ensure_ascii=False)+'\n');log.flush()
                    expected={'home':0,'library':1,'open':2,'continue':2,'next':2,'prev':2,'settings':6,'transfer':9,'maintenance':15,'network':16,'network-scan':16,'network-view':16}.get(name)
                    if expected is not None and app.get('screen')!=expected:raise RuntimeError('Incoherent command/frame receipt: '+json.dumps(entry,ensure_ascii=False))
                    if state.get('last_action_error') or app.get('font_error') or app.get('error'):raise RuntimeError(entry)
                    samples.append(entry);print(name,entry['seconds'],flush=True);return state
                time.sleep(.15)
            raise TimeoutError(name)
        for _ in range(args.rounds):
            for page in ('home','settings','maintenance','library','transfer'):action(page)
        action('library');action('open','0')
        for _ in range(args.rounds):action('next');action('prev')
        action('home');action('continue');action('home')
        if args.network:
            action('network');action('network-scan')
            deadline=time.monotonic()+25
            while True:
                state=query('paper.network');log.write(json.dumps({'network':state},ensure_ascii=False)+'\n');log.flush()
                if not state['network']['busy']:
                    if state['network']['message']!='扫描完成，请选择网络':raise RuntimeError(state)
                    break
                assert query('ping')['boot_id']==boot
                if time.monotonic()>deadline:raise TimeoutError('network scan')
                time.sleep(.5)
            action('network-view')
        final=query('status');log.write(json.dumps({'final':final},ensure_ascii=False)+'\n');log.flush()
        summary={name:round(statistics.median([s['seconds'] for s in samples if s['action']==name]),3) for name in sorted({s['action'] for s in samples})}
        print(json.dumps(summary,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
