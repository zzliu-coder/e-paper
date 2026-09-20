import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
import paper_action as pa

def test_read_retry_never_replays_command(monkeypatch):
    calls=[];token=None;reads=0
    def query(cmd,**kw):
        nonlocal token,reads
        calls.append(cmd)
        if cmd=='paper.command':token=kw['request_token'];return {}
        reads+=1
        if reads==2:raise RuntimeError('No reply to paper.status; request outcome is unconfirmed')
        return {'boot_id':'same','receipt_token_version':1,'performance':{'completed':int(reads>1)},
                'last_request_token':token,'last_action':'gray-test',
                'app':{'revision':int(reads>1),'presented':int(reads>1)}}
    monkeypatch.setattr(pa,'query',query)
    result=pa.action('gray-test','dots',status_retries=1)
    assert result['receipt']['boot_id']=='same'
    assert calls.count('paper.command')==1 and reads==3

def test_starting_app_waits_before_dispatch(monkeypatch):
    calls=[];token=None;reads=0
    def query(cmd,**kw):
        nonlocal reads,token
        calls.append(cmd)
        if cmd=='paper.command':token=kw['request_token'];return {}
        reads+=1
        if reads==1:return {'boot_id':'same','app':{'state':'starting'}}
        return {'boot_id':'same','receipt_token_version':1,'performance':{'completed':int(reads>2)},
                'last_request_token':token,'last_action':'test',
                'app':{'revision':int(reads>2),'presented':int(reads>2)}}
    monkeypatch.setattr(pa,'query',query);monkeypatch.setattr(pa.time,'sleep',lambda _:None)
    assert pa.action('test')['receipt']['boot_id']=='same'
    assert calls[:3]==['paper.status','paper.status','paper.command']
    assert calls.count('paper.command')==1
