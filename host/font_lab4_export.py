#!/usr/bin/env python3
"""Export the existing SD ratings through the existing USB service, without writing device data."""
import argparse,json
from pathlib import Path

def export(request):
    data=bytearray()
    for offset in range(0,262145,512):
        r=request('inkdesk.fontlab4.log',{'offset':offset})
        if r.get('error') or r.get('offset')!=offset:raise RuntimeError(r)
        chunk=bytes.fromhex(r['hex'])
        if len(chunk)!=r['bytes'] or len(chunk)>512:raise ValueError('Invalid log chunk')
        data.extend(chunk)
        if len(data)>262144:raise ValueError('Log limit exceeded')
        if len(chunk)<512:break
    if data and data[-1]!=10:raise ValueError('Interrupted final rating; preserving device log unchanged')
    for line in data.splitlines():
        row=json.loads(line)
        if row.get('schema')!=4 or not row.get('sample_id'):raise ValueError('Unknown rating schema')
    return bytes(data)

def main():
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    from service import call,DEFAULT_SOCKET
    def request(cmd,args):
        r=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
        if not r.get('ok'):raise RuntimeError(r)
        result=r['result']
        if not result.get('ok'):raise RuntimeError(result)
        return result
    data=export(request)
    with a.output.open('xb') as f:f.write(data)
    print(f'Exported {len(data)} bytes')
if __name__=='__main__':main()
