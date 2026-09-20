"""Restart the host USB owner, never the device; compare boot identity each time.
Requires an idle connected PAPER service. Leaves the final service running.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from service import call,DEFAULT_SOCKET
from paper_action import query


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--cycles',type=int,default=5);a=p.parse_args()
    if not 1<=a.cycles<=10:p.error('cycles 1..10')
    a.out.mkdir(parents=True,exist_ok=False)
    boot=query('hello')['boot_id'];rows=[]
    for i in range(a.cycles):
        call(DEFAULT_SOCKET,{'cmd':'service.stop'})
        deadline=time.monotonic()+5
        while Path(DEFAULT_SOCKET).exists() and time.monotonic()<deadline:time.sleep(.05)
        if Path(DEFAULT_SOCKET).exists():raise RuntimeError('Previous USB owner did not exit')
        process=subprocess.Popen([sys.executable,str(Path(__file__).with_name('service.py')),'serve','--port',a.port,
            '--atomic-lines','--log',str(a.out/f'cycle-{i}.jsonl')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
        deadline=time.monotonic()+15
        while not Path(DEFAULT_SOCKET).exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.05)
        hello=query('hello');row={'cycle':i,'boot_id':hello['boot_id'],'uptime_ms':hello['uptime_ms'],'service_pid':process.pid}
        rows.append(row);(a.out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
        if hello['boot_id']!=boot:raise RuntimeError('Device restarted; experiment FAIL')
    result={'result':'PASS','cycles':a.cycles,'boot_id':boot,'service_pid':process.pid,'scope':'reopen already-running application; ROM exit not covered'}
    (a.out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
