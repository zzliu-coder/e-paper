from __future__ import annotations
import hashlib,json,os,struct,sys,zlib
from pathlib import Path
import pytest
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'host'))
from fontlab4.packet import encode,decode,HEADER,region_bytes
from fontlab4.raster import Source,Spec,quantise,wrap,ALGORITHMS
from font_lab4_acceptance import app_status,verify_regions,Acceptance
from font_lab4_export import export
from fontlab4.grayscale_probe import make_probe,require_gray_backend

FONT=Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
@pytest.fixture(scope='module')
def source():
    configured=os.environ.get('FONTLAB4_TEST_FONT')
    path=Path(configured) if configured else FONT
    if not path.exists():pytest.skip('Set FONTLAB4_TEST_FONT for raster tests')
    return Source('test',path,int(os.environ.get('FONTLAB4_TEST_FACE','2' if path==FONT else '0')))

@pytest.mark.parametrize('px',range(16,41))
@pytest.mark.parametrize('algorithm',ALGORITHMS)
def test_exact_native_sizes(source,px,algorithm):
    sp=Spec('test',px,algorithm)
    g=source.glyph('警',px,algorithm)
    assert g.advance==px
    assert g.coverage.width>0 and g.coverage.height>0
    if algorithm.startswith('M-'):assert set(g.coverage.tobytes())<={0,255}
    else:assert any(0<v<255 for v in g.coverage.tobytes())

@pytest.mark.parametrize('px',[16,17,22,25,30,36,40])
def test_wrap_preserves_text(source,px):
    text='清晨（阅读），警察、器具。未末土士日目己已巳。0123456789 Il1 O0'
    lines=wrap(source,text,426,Spec('test',px))
    assert ''.join(lines)==text
    assert all(source.width(line,Spec('test',px))<=426 for line in lines)

@pytest.mark.parametrize('depth',[2,4])
def test_depth_threshold_partition(depth):
    im=Image.frombytes('L',(256,1),bytes(range(256)))
    q=quantise(im,depth)
    assert [x>=128 for x in q.tobytes()]==[x>=128 for x in range(256)]

def test_threshold_at_a8_not_a2():
    a8=Image.frombytes('L',(256,1),bytes(range(256)))
    q=quantise(a8,2)
    masks=[bytes(v>=t for v in q.tobytes()) for t in (112,128,144)]
    assert masks[0]==masks[1]==masks[2]
    raw=[bytes(v>=t for v in a8.tobytes()) for t in (112,128,144)]
    assert len(set(raw))==3

def test_native_modes_are_distinct_inputs():
    assert len(set(ALGORITHMS.values()))==5

def test_black_white_complement(source):
    sp=Spec('test',22,'N-A',112)
    black=Image.new('L',(480,80),255);white=Image.new('L',(480,80),0)
    source.draw(black,'未末警察',(5,50),sp)
    from dataclasses import replace
    source.draw(white,'未末警察',(5,50),replace(sp,inverse=True))
    assert all(a+b==255 for a,b in zip(black.tobytes(),white.tobytes()))

def test_missing_glyph_is_never_silent(source):
    assert source.missing('\U0010ffff')==['\U0010ffff']
    with pytest.raises(ValueError):source.glyph('\U0010ffff',22,'N-A')

@pytest.mark.parametrize('bad_px',[0,15,41,72])
def test_bad_size_rejected(source,bad_px):
    with pytest.raises(ValueError):source.glyph('中',bad_px,'N-A')

def simple_packet():
    img=Image.new('L',(480,800),255)
    meta={'samples':[{'box':[24,210,40,40]}]}
    return encode(meta,img)

@pytest.mark.parametrize('position',[0,8,12,16,20,24,28,32,48,-1])
def test_corruption_is_rejected(position):
    raw=bytearray(simple_packet());raw[position]^=1
    with pytest.raises((ValueError,UnicodeError,json.JSONDecodeError)):decode(raw)

@pytest.mark.parametrize('n',[0,1,31,32,100,48000])
def test_truncated_packet(n):
    with pytest.raises(ValueError):decode(simple_packet()[:n])

def test_trailing_packet():
    with pytest.raises(ValueError):decode(simple_packet()+b'x')

def test_no_gray_dithering():
    with pytest.raises(ValueError):encode({'samples':[]},Image.new('L',(480,800),128))

def test_nested_status():
    value={'ui_demo':{'page':8}}
    assert app_status({'app':value})==value
    with pytest.raises(ValueError):app_status({'ui_demo':{}})

def test_sample_hash_not_page_button_hash():
    img=Image.new('1',(480,800),1);box=[24,210,100,70]
    row={'sample_id':'test','box':box,'region_sha256':hashlib.sha256(region_bytes(img,box)).hexdigest(),
         'requested_px':25,'resolved_px':25,'valid':True}
    img.putpixel((20,30),0)
    assert verify_regions(img,[row])[0]['pixel_match']
    img.putpixel((30,220),0)
    with pytest.raises(AssertionError):verify_regions(img,[row])

def test_bad_identity_blocks_acceptance():
    img=Image.new('1',(480,800),1);box=[24,210,100,70]
    row={'sample_id':'test','box':box,'region_sha256':hashlib.sha256(region_bytes(img,box)).hexdigest(),
         'requested_px':25,'resolved_px':28,'valid':True}
    with pytest.raises(AssertionError):verify_regions(img,[row])

def test_log_export():
    raw=b'{"schema":4,"sample_id":"x"}\n'
    def request(cmd,args):
        assert cmd=='inkdesk.fontlab4.log';off=args['offset'];chunk=raw[off:off+512]
        return {'hex':chunk.hex(),'bytes':len(chunk),'offset':off}
    assert export(request)==raw

def test_partial_log_rejected():
    def request(cmd,args):return {'offset':0,'hex':b'{"schema":4}'.hex(),'bytes':12}
    with pytest.raises(ValueError):export(request)

def test_gray_probe_is_never_sent_to_i1():
    data=make_probe();assert len(data)==96000
    assert set(data)=={0,85,170,255}
    with pytest.raises(RuntimeError):require_gray_backend({'output_bpp':1})
    require_gray_backend({'output_bpp':2,'panel_id_verified':True,'waveform_id':'test-only'})

def test_timeout_retry_uses_app_nesting(tmp_path):
    # Model the old script's exact timeout branch, using no real device or sleeps.
    replies={'app':{'ui_demo':{'pixel_revision':8,'page':8,'pending':False,'accepting_input':True}}}
    class Stub(Acceptance):
        def __init__(self):super().__init__(None,tmp_path);self.n=0;self.taps=0
        def wait(self,*args,**kwargs):
            self.n+=1
            if self.n==1:return {'ui_demo':{'pixel_revision':7,'page':0}}
            raise TimeoutError()
        def call(self,cmd,**args):
            if cmd=='inkdesk.tap':self.taps+=1;return {}
            return replies
    stub=Stub();actual=stub.tap_until(10,10,lambda a:a['ui_demo']['page']==8)
    assert actual['ui_demo']['page']==8 and stub.taps==1


def test_shared_a8_master_for_depth_and_polarity(source):
    from dataclasses import replace
    spec=Spec('test',30)
    keys={source.master_hash('阅读警察',replace(spec,input_bpp=bpp,inverse=inv)) for bpp in (2,4,8) for inv in (False,True)}
    assert len(keys)==1


def test_manifest_identity_matches():
    from font_lab4_acceptance import verify_manifest_page
    row={'sample_id':'exact','requested_px':25,'resolved_px':25,'box':[24,210,100,70],'region_sha256':'a'*64}
    lab={'build_id':'b'*32,'page_id':0,'group':0,'samples':[row]}
    manifest={'build_id':'b'*32,'pages':[{'page_id':0,'group':0,'samples':[dict(row)]}]}
    assert verify_manifest_page(lab,manifest)
    row['sample_id']='different'
    with pytest.raises(AssertionError):verify_manifest_page(lab,manifest)

def test_manifest_wrong_bundle_rejected():
    from font_lab4_acceptance import verify_manifest_page
    with pytest.raises(RuntimeError):verify_manifest_page({'build_id':'other'},{'build_id':'expected'})
