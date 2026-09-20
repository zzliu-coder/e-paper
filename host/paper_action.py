"""Submit one PAPER action and wait for its matching frame receipt."""
import argparse
import json
import time
import uuid


def query(command,**params):
    from service import call,DEFAULT_SOCKET
    r=call(DEFAULT_SOCKET,{'cmd':command,'args':params})
    if not r.get('ok') or not r.get('result',{}).get('ok'):raise RuntimeError(r)
    return r['result']


def action(name,value='',timeout=300):
    before=query('paper.status');start=time.monotonic()
    if before.get('receipt_token_version')!=1:raise RuntimeError('This tool requires perf9 token receipts; firmware was not changed')
    token=uuid.uuid4().hex
    query('paper.command',action=name,value=value,request_token=token)
    while time.monotonic()-start<timeout:
        s=query('paper.status')
        if s['boot_id']!=before['boot_id']:raise RuntimeError('Device restarted; outcome unconfirmed')
        if s['performance']['completed']>before['performance']['completed']:
            if s.get('last_request_token')!=token:raise RuntimeError('Different request completed; outcome unconfirmed')
            if s['last_action']!=name:raise RuntimeError('Interfering input; outcome unconfirmed')
            if s.get('last_action_error'):raise RuntimeError(s['last_action_error'])
            if s['app']['revision']==s['app']['presented'] and s['app']['revision']>before['app']['revision']:
                if s['app'].get('error') or s['app'].get('font_error'):raise RuntimeError('Application rendering failed')
                expected={'home':0,'library':1,'open':2,'continue':2,'next':2,'prev':2}.get(name)
                if expected is not None and s['app'].get('screen')!=expected:raise RuntimeError('Command completed on the wrong screen')
                if name in ('next','prev'):
                    old=before['app']['reader'];new=s['app']['reader']
                    if (old['chapter'],old['offset'],old.get('page_hint'))==(new['chapter'],new['offset'],new.get('page_hint')):raise RuntimeError('Page did not move (boundary or ignored action); not a successful turn')
                return {'seconds':time.monotonic()-start,'receipt':s}
        time.sleep(.2)
    raise TimeoutError(name+' outcome unconfirmed; do not blindly retry')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action');p.add_argument('value',nargs='?',default='');p.add_argument('--timeout',type=int,default=300)
    a=p.parse_args();print(json.dumps(action(a.action,a.value,a.timeout),ensure_ascii=False))
