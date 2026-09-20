"""Live PAPER checks through the persistent USB service; never flashes firmware."""
import argparse,datetime,hashlib,json,time
from pathlib import Path
from service import call,DEFAULT_SOCKET

def main():
    p=argparse.ArgumentParser();p.add_argument('--socket',default=DEFAULT_SOCKET);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('x') as log:
        def query(cmd,**args):
            r=call(a.socket,dict(cmd=cmd,args=args))
            log.write(json.dumps(dict(time=time.time(),cmd=cmd,args=args,result=r),ensure_ascii=False)+'\n');log.flush()
            if not r.get('ok'):raise RuntimeError(r)
            return r['result']
        def action(name,value=''):
            old=query('paper.status')['app']['revision'];query('paper.command',action=name,value=value)
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                state=query('paper.status');app=state['app']
                if app['revision']>old and state.get('last_action')==name and app['presented']==app['revision']:
                    if state.get('last_action_error') or app['font_error'] or app['error']:raise RuntimeError(state)
                    print('PASS 页面/操作:',name,flush=True);return state
                time.sleep(.5)
            raise TimeoutError(name)
        first=query('paper.status');boot=first['boot_id']
        maintenance=query('paper.maintenance')['maintenance']
        if maintenance['phase'] in ('checking','writing'):
            raise RuntimeError('设备正在执行深度检查或更新，请完成后再单独运行页面验收')
        if not maintenance['awake']:action('maintenance-awake')
        for page in ('home','settings','maintenance','library','transfer','home'):action(page)
        books=query('paper.file',op='list',path='books')
        # New isolated diagnostic record; never overwrite or delete user material.
        payload='纸间维护通道：传输与完整性校验。\n'.encode();sha=hashlib.sha256(payload).hexdigest()
        dest='exports/maintenance-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'.txt'
        tid=query('paper.transfer',op='begin',path=dest,size=len(payload),sha256=sha)['transfer_id']
        query('paper.transfer',op='chunk',transfer_id=tid,offset=0,hex=payload.hex())
        receipt=query('paper.transfer',op='commit',transfer_id=tid)
        assert receipt['sha256']==sha
        assert query('paper.file',op='hash',path=dest)['sha256']==sha
        for _ in range(30):
            assert query('ping')['boot_id']==boot
            time.sleep(.5)
        assert not query('paper.status')['app']['font_error']
        action('home')
        print('PASS: 页面字体无缺失、SD 写回校验、30 次连续心跳；物理显示与触摸仍需观察。',flush=True)
if __name__=='__main__':main()
