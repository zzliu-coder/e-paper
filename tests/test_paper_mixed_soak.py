import io,json
import pytest
from paper_stability_monitor import sample_health

def test_mixed_exercises_and_real_clock_boundary():
    now=[0.0];runs=[];sink=io.StringIO()
    def query(command):
        if command=='status':return {'boot_id':'one','board_ready':True,'dropped':0,'heap_free':100,'psram_free':1000}
        return {'boot_id':'one','app':{'error':'','font_error':'','revision':1,'presented':1}}
    result=sample_health(query,sink,601,30,lambda:now[0],lambda t:now.__setitem__(0,now[0]+t),lambda:runs.append(now[0]) or {'ok':True})
    assert result['seconds']>=601 and result['exercise_count']==2 and len(runs)==2
    assert len([r for r in map(json.loads,sink.getvalue().splitlines()) if 'exercise' in r])==2

def test_exercise_failure_is_preserved():
    now=[0.0];sink=io.StringIO()
    def query(command):
        if command=='status':return {'boot_id':'one','board_ready':True,'dropped':0,'heap_free':100,'psram_free':1000}
        return {'boot_id':'one','app':{}}
    def fail():raise RuntimeError('page mismatch')
    with pytest.raises(RuntimeError,match='page mismatch'):
        sample_health(query,sink,400,30,lambda:now[0],lambda t:now.__setitem__(0,now[0]+t),fail)
    assert json.loads(sink.getvalue().splitlines()[-1])['result']=='FAIL'
