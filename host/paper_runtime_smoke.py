"""Bounded real-device concurrency smoke through the single-owner service.
No firmware write, audio capture, pairing or network credential access.
"""
import argparse,json,time
from service import call,DEFAULT_SOCKET
from paper_action import action as verified_action

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    with open(a.out,'x') as log:
        boot=None
        def q(cmd,**args):
            r=call(DEFAULT_SOCKET,dict(cmd=cmd,args=args))
            log.write(json.dumps(dict(cmd=cmd,args=args,result=r),ensure_ascii=False)+'\n');log.flush()
            if not r.get('ok') or not r.get('result',{}).get('ok'):raise RuntimeError(r)
            s=r['result']
            if boot and s.get('boot_id')!=boot:raise RuntimeError('Unexpected reboot')
            return s
        boot=q('hello')['boot_id']
        def action(name,value=''):
            result=verified_action(name,value,90);s=result['receipt']
            log.write(json.dumps(result,ensure_ascii=False)+'\n');log.flush()
            if s['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
            return s
        action('network-off')
        for _ in range(20):
            assert not q('wifi.status')['wifi']['connected'];q('ping')
        action('library');action('open','0')
        for _ in range(10):action('next');action('prev')
        action('home')
        # Ask the other UI to open during a queued PAPER refresh, then ensure
        # the PAPER surface can be restored. This is smoke, not exhaustive race proof.
        for page in ('settings','library','maintenance'):
            q('paper.command',action=page)
            time.sleep(.2)
            q('selftest.open',module='display')
            for _ in range(12):q('ping');time.sleep(.2)
            q('selftest.status')
            q('paper.open');time.sleep(2)
            action('home')
        assert q('paper.file',op='hash',path='books/systems-thinking.epub')['sha256']=='1899405b52eb921c1c78772440b0820c998229f91bbc985d42f8840aef56b786'
        final=q('status')
        assert final['touch_read_errors']==0 and final['input_queue_depth']==0
        print('PASS: Wi-Fi off/status, 20 flips, cross-UI recovery smoke, book SHA, stable boot')
if __name__=='__main__':main()
