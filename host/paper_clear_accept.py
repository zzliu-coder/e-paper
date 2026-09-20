"""Compare binary text modes on the same real book page; no firmware writes."""
import argparse,json
from paper_action import query,action

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
    with open(a.out,'x') as f:
        def record(name,r):
            f.write(json.dumps({'check':name,'result':r},ensure_ascii=False)+'\n');f.flush();return r
        before=record('before',query('paper.status'))
        if not before['app']['reader']['open'] or before['app']['screen']!=2:
            raise RuntimeError('Open a real book page first')
        boot=before['boot_id'];reader=before['app']['reader'];frames={}
        for mode in ('mono','dots','mono','dots'):
            result=record(mode,action('text-render',mode,90));s=result['receipt']
            assert s['boot_id']==boot and s['app']['screen']==2
            for key in ('chapter','offset','page_hint','style'):
                assert s['app']['reader'][key]==reader[key],key
            assert s['app']['settings']['text_render']==mode
            crc=s['app']['frame_crc']
            if mode in frames:assert frames[mode]==crc,'non-deterministic frame'
            frames[mode]=crc
        assert frames['mono']!=frames['dots'],'No visible raster difference'
        record('next',action('next',timeout=90));record('prev',action('prev',timeout=90))
        # Same page after round trip. Both page turning and rendering remain live.
        final=record('final',query('paper.status'))
        assert final['boot_id']==boot and final['app']['settings']['text_render']=='dots'
        assert final['app']['frame_crc']==frames['dots']
        print('PASS same-page binary text comparison and next/prev; physical clarity awaits user')
if __name__=='__main__':main()
