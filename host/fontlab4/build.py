"""Generate controlled comparison pages at exact physical pixels.
Run from the SDK root: python host/build_font_lab4.py --out /path/to/sd/inkdesk/font-lab4
The output contains full-page test images and provenance, no font files.
"""
from __future__ import annotations
import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image,ImageDraw,ImageChops
from .raster import Source,Spec,ALGORITHMS,wrap,toolchain
from .packet import encode,decode,canonical,WIDTH,HEIGHT

REP=(16,18,22,30,40)
ALGO=tuple(ALGORITHMS)
BODY=('清晨，窗边的光慢慢移到桌上。我把昨天读到的一页重新翻开，先看正文，再看页码。'
      '字体放大以后，笔画之间应当留有空隙；字号缩小时，常用字仍应当容易辨认。'
      '现在请留意警察、藏书、器具、餐饮这些词，检查是否出现断笔、粘连。')
SHORT='未末 警察 Aa8'
TEXTS=['未末 土士 日目 己已巳 人入','警察 藏书 赢得 器具 餐饮 薄雾',
       '0123456789 Il1 O0 8B rn m','Wi-Fi 82% 09:41 012 / 128',
       '中文（标点），句号。问号？感叹号！']
GROUPS=['算法','字体','浓淡','字号','阅读','刷新']
UI_TEXT='返回旧版上一页下一页全刷选样本评分清楚太细太粗粘连相同已保存未保存失败组快刷黑白技术位深覆盖率同源固定字号模式第页实验同字体仅改变粗细资源样本不可用宋体对照'

class Builder:
    def __init__(self,sources,out,include_weight=True):
        self.sources=sources; self.lookup={s.key:s for s in sources}; self.out=Path(out)
        self.ui=sources[0]; self.pages=[]; self.sample_records={}
        self.include_weight=include_weight
        self.toolchain=toolchain()
        raw={'sources':[s.info() for s in sources], 'toolchain':self.toolchain,
             'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
             'texts':[BODY,SHORT,*TEXTS], 'representative_sizes':REP, 'all_sizes':list(range(16,41))}
        self.build_id=hashlib.sha256(canonical(raw)).hexdigest()[:32]
        self.build_info=raw

    def ui_text(self,img,text,x,y,px=18,black=True):
        sp=Spec(self.ui.key,px=px,inverse=not black)
        top,_=self.ui.line_metrics(text or 'A',sp)
        self.ui.draw(img,text,(x,y+top),sp)

    def canvas(self,title,group,index,subtitle):
        img=Image.new('L',(480,800),255); draw=ImageDraw.Draw(img)
        self.ui_text(img,'返回',20,18,20)
        self.ui_text(img,title,108,16,22)
        self.ui_text(img,'旧版',408,18,20)
        for i,name in enumerate(GROUPS):
            box=(20+i*74,66,89+i*74,113)
            draw.rectangle(box,fill=0 if group==i else 255,outline=0)
            self.ui_text(img,name,30+i*74,80,20,black=group!=i)
        for x,w,text in ((20,100,'上一页'),(184,112,'全刷'),(360,100,'下一页')):
            draw.rectangle((x,122,x+w-1,169),outline=0)
            self.ui_text(img,text,x+14,136,18)
        self.ui_text(img,subtitle,24,177,16)
        self.ui_text(img,f'Page {index:04d}',322,699,16)
        for i,text in enumerate(('清楚','太细','太粗','粘连','相同')):
            x=20+i*88;draw.rectangle((x,736,x+83,783),outline=0)
            self.ui_text(img,text,x+20,751,18)
        return img

    def sample(self,img,text,box,spec,ordinal):
        source=self.lookup[spec.font]; x,y,w,h=box
        missing=source.missing(text)
        source_info=source.info()
        flags,render=ALGORITHMS[spec.algorithm]
        record={'font_id':source.key,'font_file_sha256':source.sha256,'face_index':source.face_index,
          'weight':source.weight,'axes':source.axes,'algorithm':spec.algorithm,
          'load_flags':flags | 8, # FT_LOAD_NO_BITMAP
          'render_mode':render,'requested_px':spec.px,'resolved_px':spec.px,
          'threshold':spec.threshold,'input_bpp':1 if spec.algorithm.startswith('M-') else spec.input_bpp,
          'output_bpp':1,'polarity':'inverse' if spec.inverse else 'normal',
          'resolved_ok':not missing,'fallback_used':False,'missing_glyphs':missing,
          'text':text,'text_sha256':hashlib.sha256(text.encode()).hexdigest(),
          'hint_tables_present':source_info['hint_tables_present'],
          'master_sha256':None if missing else source.master_hash(text,spec)}
        identity=hashlib.sha256(canonical(record)).hexdigest()[:24]
        sid=f'{source.key}-{spec.px}-{spec.algorithm}-{identity}'
        record['sample_id']=sid
        # Image box includes text only; labels and selection status remain outside.
        record['box']=list(box);record['ordinal']=ordinal
        if missing:
            self.ui_text(img,'SAMPLE UNAVAILABLE',x,y,16)
        else:
            lines=wrap(source,text,w-6,spec)
            top,lh=source.line_metrics(text.replace('\n','') or 'A',spec)
            if len(lines)*lh > h:
                raise ValueError(f'Clipping: {sid}: {len(lines)}*{lh}>{h}, {text!r}')
            # Render into a padded local image and reject any ink outside the declared ROI.
            # This detects bearings/descenders as well as advance-based wrapping, without scaling.
            margin=16;background=0 if spec.inverse else 255
            local=Image.new('L',(w+2*margin,h+2*margin),background)
            for j,line in enumerate(lines):
                source.draw(local,line,(margin+3,margin+top+2+j*lh),spec)
            ink=local if spec.inverse else ImageChops.invert(local)
            bounds=ink.getbbox()
            if bounds and (bounds[0]<margin or bounds[1]<margin or bounds[2]>margin+w or bounds[3]>margin+h):
                raise ValueError(f'Ink outside sample ROI: {sid}: {bounds}')
            img.paste(local.crop((margin,margin,margin+w,margin+h)),(x,y))
        self.sample_records[sid]=dict(record)
        return record

    def add_rows(self,title,group,subtitle,rows,refresh=False,alt=False,tech=False):
        index=len(self.pages);img=self.canvas(title,group,index,subtitle)
        records=[]; n=len(rows)
        if n>5: raise ValueError('At most five rows on one small display')
        pitch=432//max(1,n)
        for i,(label,text,spec) in enumerate(rows):
            y=208+i*pitch
            self.ui_text(img,f'{i+1}. {label}',24,y,16)
            box=(24,y+24,432,pitch-28)
            records.append(self.sample(img,text,box,spec,i+1))
        if refresh:
            d=ImageDraw.Draw(img)
            for x,label in ((20,'1 组快刷'),(250,'4 组快刷')):
                d.rectangle((x,650,x+209,694),outline=0)
                self.ui_text(img,label,x+26,665,18)
        meta={'page_id':index,'group':group,'title':title,'subtitle':subtitle,
              'build_id':self.build_id,'samples':records,'refresh':refresh,
              'alternate':alt,'technical_only':tech,'alternate_id':None,'nav':{}}
        self.pages.append((meta,img))
        return index

    def add_reading(self,font,algo,threshold,px,inverse):
        spec=Spec(font,px,algo,threshold,inverse=inverse);source=self.lookup[font]
        lines=wrap(source,BODY,426,spec)
        _,lh=source.line_metrics(BODY,spec); maxlines=264//lh
        # All text is emitted across explicit pages. No hidden truncation.
        for start in range(0,len(lines),maxlines):
            title='阅读与混排';index=len(self.pages)
            subtitle=f'{font} {algo} {px}px T{threshold} '+('INV' if inverse else 'BW')
            img=self.canvas(title,4,index,subtitle)
            segment='\n'.join(lines[start:start+maxlines]);records=[]
            records.append(self.sample(img,segment,(24,211,432,264),spec,1))
            self.ui_text(img,'2. Il1 O0 / mixed',24,487,16)
            records.append(self.sample(img,'Il1 O0 8B rn m',(24,511,432,60),spec,2))
            self.ui_text(img,'3. UI / inverse',24,583,16)
            records.append(self.sample(img,'继续阅读 82%',(24,607,432,60),replace(spec,inverse=not inverse),3))
            meta={'page_id':index,'group':4,'title':title,'subtitle':subtitle,'build_id':self.build_id,
                  'samples':records,'refresh':False,'alternate':False,'alternate_id':None,
                  'technical_only':False,'reading_lines':[start,start+maxlines], 'nav':{}}
            self.pages.append((meta,img))

    def add_depth(self,font,px=30):
        i=len(self.pages); img=self.canvas('位深覆盖率',2,i,f'{font} N-A {px}px / I1 output')
        records=[]
        # Same A8 master, same dimensions, both polarities. No fontpack identity assumption.
        for j,(bpp,inv) in enumerate(((2,False),(4,False),(2,True),(4,True))):
            x=24+(j%2)*222;y=215+(j//2)*198
            self.ui_text(img,f'{j+1}. {bpp}bpp '+('INV' if inv else 'BW'),x,y,16)
            records.append(self.sample(img,'阅读\n警察 Aa8',(x,y+30,210,151),Spec(font,px,'N-A',128,bpp,inv),j+1))
        self.pages.append(({'page_id':i,'group':2,'title':'位深覆盖率','subtitle':'same A8 master / final I1',
            'build_id':self.build_id,'samples':records,'refresh':False,'alternate':False,
            'alternate_id':None,'technical_only':True,'nav':{}},img))

    def create(self):
        # First source is the primary family; all sources receive algorithm tests.
        for source in self.sources:
            for px in REP:
                self.add_rows('算法对比',0,f'{source.key} / {px}px / threshold 128',
                    [(algo,SHORT,Spec(source.key,px,algo)) for algo in ALGO])
        for algo in ALGO:
            for px in REP:
                self.add_rows('字体对比',1,f'{algo} / {px}px / threshold 128',
                    [(s.key,TEXTS[1] if px<=22 else SHORT,Spec(s.key,px,algo)) for s in self.sources])
        for source in self.sources:
            for algo in ('N-A','L-A','N-N'):
                for px in REP:
                    self.add_rows('浓淡对比',2,f'{source.key} / {algo} / {px}px / A8',
                        [(f'T{t}',TEXTS[1] if px<=22 else SHORT,Spec(source.key,px,algo,t)) for t in (112,128,144)])
            self.add_depth(source.key)
        # Weight-only comparison requires actual same-family faces; never simulate bold by dilation.
        if self.include_weight:
            families={s.family for s in self.sources}
            for family in sorted(families):
                peers=[s for s in self.sources if s.family==family]
                if len({s.weight for s in peers})>1:
                    for px in REP:
                        self.add_rows('字重对比',2,f'N-A / {px}px / T128 / same family',
                            [(f'{s.key} w{s.weight}',SHORT,Spec(s.key,px)) for s in peers])
        recipes=[(a,128) for a in ALGO]+[('N-A',112),('N-A',144)]
        for source in self.sources:
            for algo,t in recipes:
                for start in range(16,41,5):
                    self.add_rows('完整字号',3,f'{source.key} / {algo} / T{t} / {start}-{start+4}px',
                        [(f'{px}px',SHORT,Spec(source.key,px,algo,t)) for px in range(start,start+5)])
        for source in self.sources:
            for algo,t in recipes:
                for px in REP:
                    for inverse in (False,True):
                        self.add_reading(source.key,algo,t,px,inverse)
        for source in self.sources:
            for px in REP:
                spec=Spec(source.key,px)
                target=self.add_rows('刷新残影',5,f'{source.key} / N-A / {px}px / target',
                    [('Target',SHORT,spec),('Dense',TEXTS[1],spec),('Inverse',SHORT,replace(spec,inverse=True))],refresh=True)
                other=self.add_rows('刷新残影',5,f'{source.key} / N-A / {px}px / alternate',
                    [('Target',SHORT,replace(spec,inverse=True)),('Dense',TEXTS[1],replace(spec,inverse=True)),('Inverse',SHORT,spec)],refresh=True,alt=True)
                self.pages[target][0]['alternate_id']=other
                self.pages[other][0]['alternate_id']=target
        visible=[m['page_id'] for m,_ in self.pages if not m['alternate']]
        groups=[[m['page_id'] for m,_ in self.pages if m['group']==g and not m['alternate']] for g in range(6)]
        for meta,img in self.pages:
            pid=meta['page_id'];base=meta['alternate_id'] if meta['alternate'] else pid
            members=groups[meta['group']];at=members.index(base)
            meta['nav']={'prev':members[(at-1)%len(members)],'next':members[(at+1)%len(members)],
                         'groups':[x[0] for x in groups]}
            meta['page_count']=len(self.pages)
        return self

    def save(self):
        if self.out.exists() and any(self.out.iterdir()):
            raise FileExistsError(f'Refusing to overwrite existing experiment: {self.out}')
        self.out.mkdir(parents=True,exist_ok=True)
        files={}; index=[]
        for meta,img in self.pages:
            name=f'{meta["page_id"]:04d}.fl4';data=encode(meta,img)
            checked,_=decode(data)
            (self.out/name).write_bytes(data)
            files[name]=hashlib.sha256(data).hexdigest()
            index.append(checked)
        manifest={'schema':4,'build_id':self.build_id,'page_count':len(index),'build':self.build_info,
                  'pages':index,'files':files,'true_grayscale':'NOT_SUPPORTED_BY_THIS_I1_ROUTE',
                  'native_pixels':True,'font_files_included':False}
        (self.out/'manifest.json').write_bytes(canonical(manifest))
        (self.out/'manifest.sha256').write_text(hashlib.sha256(canonical(manifest)).hexdigest()+'\n')
        return manifest

def default_sources(repo):
    p=repo/'use_font/ui-themes'
    rows=[('sourcehan',p/'SourceHanSansCN-Medium.otf',0),
          ('lxgw',p/'candidates/LXGWNeoXiHeiScreen.ttf',0),
          ('wqy',p/'candidates/wqy-microhei.ttc',0)]
    return [Source(key,path,face) for key,path,face in rows]

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--sources',type=Path,help='JSON array: id,path,face,optional axes. Paths relative to config file.')
    p.add_argument('--preview',type=Path,help='Write representative PNG pages (display samples, not font files)')
    a=p.parse_args(argv)
    if a.sources:
        rows=json.loads(a.sources.read_text());sources=[]
        for r in rows:
            path=Path(r['path']);path=path if path.is_absolute() else a.sources.resolve().parent/path
            sources.append(Source(r['id'],path,int(r.get('face',0)),r.get('axes')))
    else:sources=default_sources(a.repo.resolve())
    if not 1<=len(sources)<=5 or len({s.key for s in sources})!=len(sources):
        raise ValueError('One to five uniquely named sources required')
    import re
    if any(not re.fullmatch('[a-zA-Z0-9_-]{1,16}',s.key) for s in sources): raise ValueError('Invalid source ID')
    missing=sources[0].missing(UI_TEXT+''.join(GROUPS)+BODY+SHORT)
    if missing: raise ValueError(f'UI font lacks characters: {missing}')
    b=Builder(sources,a.out).create();m=b.save()
    if a.preview:
        a.preview.mkdir(parents=True,exist_ok=True)
        for group in range(6):
            choices=[(meta,img) for meta,img in b.pages if meta['group']==group and not meta['alternate']]
            meta,img=choices[min(2,len(choices)-1)]
            img.save(a.preview/f'{group+1:02d}-{meta["page_id"]:04d}.png')
    print(json.dumps({'build_id':m['build_id'],'pages':m['page_count'],'out':str(a.out)},ensure_ascii=False))
    return m
