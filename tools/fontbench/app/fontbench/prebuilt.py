"""Exact sample images bundled for offline use, never a replacement font."""
from pathlib import Path
import hashlib,json,zlib
from .model import ALGS,TEXTS

class PrebuiltSource:
    prebuilt=True
    def __init__(self,bank,fi,info,toolchain):
        self.bank=Path(bank);self.fi=fi;self.meta=info;self.toolchain=toolchain
    def info(self):return self.meta|{'source_mode':'prebuilt-samples'}
    def sample(self,px,algorithm,content,kind):
        from .bank import decode,filename
        manifest=json.loads((self.bank/'manifest.json').read_text())
        weight=int(self.meta.get('axes',{}).get('wght',self.meta['weight']))
        path=self.bank/'tiles'/filename(self.fi,weight,px,ALGS.index(algorithm),list(TEXTS).index(content),int(kind=='wide'))
        padded=False
        if kind=='wide' and content!='body' and not path.is_file():
            path=self.bank/'tiles'/filename(self.fi,weight,px,ALGS.index(algorithm),list(TEXTS).index(content),0);padded=True
        if not path.is_file():raise ValueError('此固定样文尚无离线样本；不会用其他字体代替')
        raw,(w,h),_=decode(path.read_bytes(),manifest['bundle_tag'])
        if padded:
            canvas=bytearray(432*224)
            for y in range(h):canvas[y*432:y*432+w]=raw[y*w:(y+1)*w]
            raw=bytes(canvas);w,h=432,224
        # Exact text is supplied by build manifest. Layout bounds are only retained for live rasterization.
        text='\n'.join(TEXTS[content]) if content!='body' else ('清晨光线\n慢慢移来' if kind=='compact' else TEXTS['body'][0])
        return raw,{'width':w,'height':h,'source':self.info(),'visible_text':text,
                    'text_sha256':hashlib.sha256(text.encode()).hexdigest(),'master_sha256':hashlib.sha256(raw).hexdigest(),
                    'master_crc32':zlib.crc32(raw),'requested_px':px,'resolved_px':px,'kind':kind,
                    'body_continues':content=='body','source_toolchain':self.toolchain}

def merge(registry,bank):
    bank=Path(bank)
    if not (bank/'manifest.json').is_file():return
    doc=json.loads((bank/'manifest.json').read_text())
    if doc.get('schema') not in (5,6):return
    for fi,c in enumerate(doc['catalog']):
        for info in c.get('faces',[]):
            weight=int(info.get('axes',{}).get('wght',info['weight']))
            registry.faces.setdefault((c['id'],weight),PrebuiltSource(bank,fi,info,doc['toolchain']))
