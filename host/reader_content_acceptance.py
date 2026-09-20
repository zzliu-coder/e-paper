"""Upload only a new SDK test EPUB, verify SHA, test text/image navigation via the owner service."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from service import call, DEFAULT_SOCKET
from paper_action import action as verified_action


def location(receipt):
    reader = receipt['app']['reader']
    return reader['chapter'], reader['offset'], reader.get('page_hint', -1)



def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixture',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    data=a.fixture.read_bytes();sha=hashlib.sha256(data).hexdigest()
    name='SDK-reader-'+sha[:12]+'.epub';target='books/'+name
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('x') as log:
        def query(cmd,**args):
            r=call(DEFAULT_SOCKET,{'cmd':cmd,'args':args})
            if not r.get('ok') or not r.get('result',{}).get('ok'):raise RuntimeError(r)
            return r['result']
        boot=query('hello')['boot_id']
        def action(cmd,value=''):
            result=verified_action(cmd,value,timeout=300)
            s=result['receipt']
            if s['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
            log.write(json.dumps({'action':cmd,'value':value,**result},ensure_ascii=False)+'\n');log.flush()
            if s['app'].get('reading_content',{}).get('image_error'):
                raise RuntimeError(s['app']['reading_content']['image_error'])
            return s
        action('home')
        files=[];offset=0
        while True:
            r=query('paper.file',op='list',path='books',offset=offset);files+=r['files'];offset=len(files)
            if offset>=r['total']:break
        if not any(f['name']==name for f in files):
            t=query('paper.transfer',op='begin',path=target,size=len(data),sha256=sha)['transfer_id']
            try:
                for off in range(0,len(data),512):query('paper.transfer',op='chunk',transfer_id=t,offset=off,hex=data[off:off+512].hex())
                receipt=query('paper.transfer',op='commit',transfer_id=t)
                assert receipt['sha256']==sha
            except Exception:
                query('paper.transfer',op='cancel',transfer_id=t)
                raise
            files=[];offset=0
            while True:
                r=query('paper.file',op='list',path='books',offset=offset);files+=r['files'];offset=len(files)
                if offset>=r['total']:break
        assert query('paper.file',op='hash',path=target)['sha256']==sha
        files=[f for f in files if f['directory'] or Path(f['name']).suffix.lower() in ('.epub','.txt')]
        index=next(i for i,f in enumerate(files) if f['name']==name)
        action('library');first=action('open',str(index))
        image=first if first['app'].get('reading_content',{}).get('image_page') else None
        visited=[first]
        # Composition varies with font/engine: discover rather than assume 3 pages.
        for _ in range(32):
            content=visited[-1]['app'].get('reading_content',{})
            if image is not None and len(visited)>1:break
            if content.get('complete') and content.get('page',-1)+1>=content.get('pages_built',0):
                break
            current=action('next');visited.append(current)
            if current['app'].get('reading_content',{}).get('image_page'):image=current
        if image is None:raise RuntimeError('Illustration not observed within bounded navigation')
        saved=location(visited[-1])
        action('home');resumed=action('continue')
        if location(resumed)!=saved:raise RuntimeError('Position changed on reopen')
        for expected in reversed(visited[:-1]):
            previous=action('prev')
            if location(previous)!=location(expected):raise RuntimeError('Back-navigation locator mismatch')
        if location(query('paper.status'))!=location(first):raise RuntimeError('Did not return to first tested page')
        result={'result':'PASS','boot_id':boot,'book':target,'sha256':sha,'physical_display':'NOT_PROVEN'}
        log.write(json.dumps(result)+'\n');print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
