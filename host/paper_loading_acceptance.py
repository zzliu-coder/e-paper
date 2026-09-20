"""Exercise real loading receipts, cancellation and retry through the USB owner.
Does not flash, change font preferences or move the reading position.
"""
import argparse
import json
import time
import uuid
from pathlib import Path
from service import call, DEFAULT_SOCKET

def loading_sample(loading):
    # Completed jobs retain their counters; they are not feedback for a new job.
    if not loading.get('active'):return False,0
    return bool(loading.get('frames')),loading.get('bytes_done',0)

def cancellation_ready(loading):
    # Bounded verification may read far less than a full-font scan. Cancel a
    # genuinely active job after observable progress, not an arbitrary byte size.
    visible,done=loading_sample(loading)
    return bool(loading.get('active') and (visible or done>0))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('x') as log:
        def query(cmd,**args):
            reply=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
            if not reply.get('ok') or not reply.get('result',{}).get('ok'):raise RuntimeError(reply)
            return reply['result']
        first=query('paper.status');boot=first['boot_id']
        if first.get('receipt_token_version')!=1:raise RuntimeError('Requires perf9 token receipts')
        if not first['app']['reader']['open']:raise RuntimeError('Open a book first')
        position={k:first['app']['reader'].get(k) for k in ('engine','chapter','offset','page_hint','style')}
        def action(name,value='',cancel=False):
            before=query('paper.status');started=time.monotonic();cancel_sent=False;first_feedback=None;max_progress=0
            if before.get('loading',{}).get('active'):raise RuntimeError('Wait for the current loading job before testing')
            token=uuid.uuid4().hex
            query('paper.command',action=name,value=value,request_token=token)
            while time.monotonic()-started<120:
                s=query('paper.status')
                if s['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
                loading=s.get('loading',{})
                visible,done=loading_sample(loading)
                if visible and first_feedback is None:first_feedback=time.monotonic()-started
                max_progress=max(max_progress,done)
                if cancel and cancellation_ready(loading) and not cancel_sent:
                    query('paper.command',action='home');cancel_sent=True
                if s['performance']['completed']>before['performance']['completed']:
                    if s['last_action']!=name or s.get('last_request_token')!=token:raise RuntimeError('Interfering input')
                    if s['app']['revision']!=s['app']['presented']:raise RuntimeError('Frame not acknowledged')
                    row={'action':name,'value':value,'cancel_requested':cancel_sent,'seconds':time.monotonic()-started,
                         'first_feedback_seconds':first_feedback,'max_bytes':max_progress,'receipt':s}
                    log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
                    if cancel:
                        if not cancel_sent or s['app']['screen']!=0 or not s.get('last_action_error'):raise RuntimeError('Cancellation not demonstrated')
                    elif s.get('last_action_error') or s['app'].get('error') or s['app'].get('font_error'):raise RuntimeError(row)
                    return row
                time.sleep(.1)
            raise TimeoutError(name)
        action('home');action('font-verify-mode','single')
        action('continue',cancel=True)
        cold=action('continue')
        actual={k:cold['receipt']['app']['reader'].get(k) for k in position}
        if actual!=position:raise RuntimeError('Cancel/retry changed position or font')
        action('home');warm=action('continue')
        if {k:warm['receipt']['app']['reader'].get(k) for k in position}!=position:raise RuntimeError('Warm reopen changed position')
        result={'result':'PASS','boot_id':boot,'position_preserved':True,'cold_seconds':cold['seconds'],
                'first_feedback_seconds':cold['first_feedback_seconds'],'warm_seconds':warm['seconds'],
                'warm_progress_frames':warm['receipt']['loading']['frames'],
                'physical_touch_and_legibility':'NOT_PROVEN'}
        log.write(json.dumps(result)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
