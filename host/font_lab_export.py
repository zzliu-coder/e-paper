"""Read real font-lab ratings through the existing single-owner USB service."""
import argparse
import hashlib
import json
from pathlib import Path
from service import call, DEFAULT_SOCKET

def collect(request):
    data=bytearray();boot=None
    while True:
        response=request('inkdesk.fontlab.log',offset=len(data))
        if not response.get('ok') or response.get('error'):raise RuntimeError(response)
        if response.get('device_id')!='1020ba6e0be0':raise RuntimeError('wrong device')
        if boot is None:boot=response['boot_id']
        if response['boot_id']!=boot:raise RuntimeError('reboot during export')
        chunk=bytes.fromhex(response['hex'])
        if response['offset']!=len(data) or len(chunk)!=response['bytes']:raise ValueError('invalid chunk')
        data.extend(chunk)
        if len(data)>263168:raise ValueError('log exceeded bounded limit')
        if not chunk:break
    for line in data.splitlines():json.loads(line)
    return bytes(data)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
    def request(cmd,**args):
        response=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
        if not response.get('ok'):raise RuntimeError(response)
        return response['result']
    data=collect(request)
    # Never silently overwrite an earlier observation.
    with a.output.open('xb') as output:output.write(data)
    print(json.dumps({'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'file':str(a.output)}))
