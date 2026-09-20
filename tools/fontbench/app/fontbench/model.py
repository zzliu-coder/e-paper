"""Strict configuration and sensitivity-matrix semantics. No hidden font fallback."""
from __future__ import annotations
from dataclasses import asdict, dataclass, replace
from itertools import product
import hashlib, json

ALGS = ('M-A','M-N','N-A','L-A','N-N','O-N','FW')
RASTER_ALGS = ALGS[:-1]
OUTPUTS = ('mono','gray4')
FW_SIZES = (18,20,22,25,28,30)
ALG_NAMES = ('自动对齐·黑白','字体自带·黑白','自动对齐·标准','轻微对齐','字体自带·标准','原轮廓·无微调','固件原样·UI')
SIZES = tuple(range(16,41))
TONES = (144,128,112)
WEIGHTS = (400,500,700)
TEXTS = {
 'common': ('阅读设置','清晨光线'),
 'dense': ('警器藏赢','餐饮薄雾'),
 'mixed': ('中文 Aa','Il1 O0'),
 'body': ('清晨，窗边的光慢慢移到桌上。我把昨天读到的一页重新翻开。阅读时，先看正文，再看页码。请留意警察、藏书、器具、餐饮，看看笔画是否清楚。',),
}
TEXT_NAMES = {'common':'常用字','dense':'密笔画','mixed':'中英混排','body':'连读正文'}
FIELDS = ('font','px','algorithm','threshold','weight','output')
@dataclass(frozen=True)
class Spec:
    font: str='noto'
    px: int=22
    algorithm: str='N-A'
    threshold: int=128
    weight: int=400
    content: str='common'
    inverse: bool=False
    input_bpp: int=8
    output: str='mono'
    def __post_init__(self):
        if not isinstance(self.font,str) or not self.font or len(self.font)>48 or not all(c.isalnum() or c in '-_' for c in self.font):
            raise ValueError('字体 ID 无效')
        for k,values in [('px',SIZES),('threshold',TONES),('weight',WEIGHTS),('input_bpp',(2,4,8))]:
            v=getattr(self,k)
            if type(v) is not int or v not in values: raise ValueError(f'{k} 超出支持范围')
        if self.algorithm not in ALGS or self.content not in TEXTS or type(self.inverse) is not bool or self.output not in OUTPUTS:
            raise ValueError('算法、样文或底色无效')
    @classmethod
    def parse(cls,obj):
        if not isinstance(obj,dict) or set(obj)-set(cls.__dataclass_fields__): raise ValueError('配置字段无效')
        return cls(**obj)
    def data(self): return asdict(self)
    def key(self): return hashlib.sha256(canonical(self.data())).hexdigest()[:20]
    def with_value(self,field,value):
        if field not in FIELDS: raise ValueError('比较维度无效')
        return replace(self,**{field:value})

def canonical(value): return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()

def matrix(base: Spec,row_axis: str,col_axis: str,rows:list,cols:list,max_cells=256):
    if row_axis not in FIELDS or col_axis not in (*FIELDS,'none') or row_axis==col_axis: raise ValueError('横轴和纵轴须选择不同参数')
    if not isinstance(rows,list) or not isinstance(cols,list) or not rows or not cols: raise ValueError('至少选择一个行值和列值')
    if len(rows)*len(cols)>max_cells: raise ValueError('单次最多显示 256 个样本，请减少选项')
    if len(set(map(str,rows)))!=len(rows) or len(set(map(str,cols)))!=len(cols): raise ValueError('重复的比较值')
    out=[]
    for r,c in product(rows,cols):
        s=base.with_value(row_axis,r)
        if col_axis!='none':s=s.with_value(col_axis,c)
        out.append(s)
    return out

def equivalent_recipe(a,b):
    """MONO has no coverage intermediates: tones are explicitly equivalent."""
    if a.algorithm in ('M-A','M-N') and b.algorithm==a.algorithm:
        return replace(a,threshold=128,input_bpp=8)==replace(b,threshold=128,input_bpp=8)
    return a==b


def firmware_compatible(s: Spec) -> bool:
    """Only the audited UI Medium assets; no size mapping, no substitute reader font."""
    return (s.font=='sourcehan' and s.weight==500 and s.px in FW_SIZES and
            s.threshold==128 and s.input_bpp==2 and s.output=='mono')

def firmware_spec(px=22,content='common',inverse=False):
    return Spec(font='sourcehan',weight=500,px=px,algorithm='FW',threshold=128,
                input_bpp=2,content=content,inverse=inverse)
