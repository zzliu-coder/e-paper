"""Local outline discovery plus explicit downloads; no silent font substitution."""
from __future__ import annotations
from pathlib import Path
import hashlib, json, os, re, tempfile, urllib.request, zipfile
from fontTools.ttLib import TTFont, TTCollection
from .raster import Source
from .model import TEXTS

CATALOG = [
 {'id':'sourcehan','name':'思源黑体','short':'思源黑体','match':['SourceHanSans','Source Han Sans'],'site':'https://github.com/adobe-fonts/source-han-sans','note':'与 Noto CJK 同源，作为版本/字重对照。',
  'downloads':[(f'https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.005R/SubsetOTF/CN/SourceHanSansCN-{w}.otf',f'SourceHanSansCN-{w}.otf') for w in ('Regular','Medium','Bold')]},
 {'id':'misans','name':'MiSans 小米黑体','short':'MiSans','match':['MiSans'],'site':'https://hyperos.mi.com/font/zh/download','note':'可载入本机 MiSans 静态或可变字体；不在本包分发字体软件。','downloads':[]},
 {'id':'harmony','name':'HarmonyOS Sans SC','short':'鸿蒙黑体','match':['HarmonyOS Sans SC','HarmonyOS_Sans_SC','HarmonyOSSansSC'],'site':'https://github.com/openharmony/resources','note':'仅选择简体中文 SC 字面。',
  'downloads':[(f'https://raw.githubusercontent.com/openharmony/resources/master/fonts/HarmonyOS_Sans_SC_{w}.ttf',f'HarmonyOS_Sans_SC_{w}.ttf') for w in ('Regular','Medium','Bold')]},
 {'id':'lxgw','name':'霞鹜新晰黑·屏幕版','short':'新晰黑','match':['LXGW Neo XiHei','LXGWNeoXiHei','霞鹜新晰黑'],'site':'https://github.com/lxgw/LxgwNeoXiZhi-Screen','note':'历史屏幕版；实际字重按原文件记录，不造出不存在的 Medium。',
  'downloads':[('https://github.com/lxgw/LxgwNeoXiZhi-Screen/releases/download/26.08.21/LXGWNeoXiHeiScreen.ttf','LXGWNeoXiHeiScreen.ttf')]},
 {'id':'wqy','name':'文泉驿微米黑','short':'文泉驿','match':['WenQuanYi Micro Hei','文泉驿微米黑','wqy-microhei'],'site':'https://github.com/anthonyfok/fonts-wqy-microhei','note':'紧凑的黑体候选；优先读取仓库已有 TTC。','downloads':[]},
 {'id':'pingfang','name':'苹方 SC（本机）','short':'苹方黑体','match':['PingFang SC','PingFangSC'],'site':'https://support.apple.com/zh-cn/','note':'仅使用你 Mac 上已有的苹方 SC。保留实际字重，不分发字体文件。','downloads':[]},
 {'id':'heiti','name':'黑体 SC（本机）','short':'黑体 SC','match':['Heiti SC','HeitiSC','STHeitiSC'],'site':'https://support.apple.com/zh-cn/','note':'仅读取 Mac 已有黑体 SC，和苹方分开比较；不分发字体文件。','downloads':[]},
 {'id':'noto','name':'Noto Sans CJK SC','short':'Noto 对照','match':['Noto Sans CJK SC','NotoSansCJKsc','Noto Sans SC'],'site':'https://github.com/notofonts/noto-cjk','note':'附带离线试字图片；与思源同源，不重复计为独立设计家族。','downloads':[]},
]

class Registry:
    def __init__(self, repo=None, cache=None, extra=None, system=True, prebuilt=None):
        self.repo=Path(repo).expanduser().resolve() if repo else None
        self.cache=Path(cache or Path.home()/'.cache/epaper-fontbench').expanduser().resolve()
        self.extra=[Path(x).expanduser().resolve() for x in extra or []]
        self.system=system;self.prebuilt=prebuilt
        self.faces={};self.errors=[]
        self.refresh()
    def paths(self):
        roots=[self.cache/'fonts',*self.extra]
        if self.repo: roots += [self.repo/'use_font', self.repo/'fonts']
        if self.system:
            roots += [Path('/usr/share/fonts/opentype/noto'), Path('/usr/share/fonts/truetype/wqy'),
                      Path.home()/'Library/Fonts',Path('/Library/Fonts'),
                      Path('/System/Library/Fonts/PingFang.ttc'),Path('/System/Library/Fonts/STHeiti Light.ttc'),
                      Path('/System/Library/Fonts/STHeiti Medium.ttc')]
        if self.system:
            assets=Path('/System/Library/AssetsV2')
            if assets.is_dir():
                for asset in sorted(assets.glob('com_apple_MobileAsset_Font*')):
                    roots.extend(list(asset.glob('*/AssetData/PingFang.ttc'))[:32])
        seen=set()
        for root in roots:
            if root.is_file(): paths=[root]
            elif root.is_dir(): paths=sorted(root.rglob('*'))
            else:continue
            for f in paths:
                if f.suffix.lower() not in ('.ttf','.otf','.ttc') or not f.is_file():continue
                rp=f.resolve()
                if rp in seen:continue
                seen.add(rp)
                if len(seen)>512: raise ValueError('字体目录超过 512 个文件，请缩小目录范围')
                yield rp
    def refresh(self):
        self.faces={};self.errors=[]
        for path in self.paths():
            try:self._register(path)
            except Exception as e:self.errors.append({'file':path.name,'error':str(e)[:240]})
        if self.prebuilt:
            from .prebuilt import merge
            merge(self,self.prebuilt)
        return self.public()
    def _register(self,path):
        if path.stat().st_size>100*1024*1024:raise ValueError('字体过大')
        with path.open('rb') as f: is_collection=f.read(4)==b'ttcf'
        count=len(TTCollection(path,lazy=True).fonts) if is_collection else 1
        for index in range(min(count,32)):
            with TTFont(path,fontNumber=index,lazy=True) as font:
                names=font['name'];family=names.getDebugName(16) or names.getDebugName(1) or ''
                style=names.getDebugName(17) or names.getDebugName(2) or ''
                search=family+' '+path.name
                matched=next((c for c in CATALOG if any(t.lower() in search.lower() for t in c['match'])),None)
                if not matched: continue
                # Prevent JP/KR/TC and mono variants being labelled as simplified proportional SC.
                if matched['id']=='noto' and ('Mono' in family or not (' SC' in family or 'CJKsc' in path.name or family=='Noto Sans SC')):continue
                if matched['id']=='wqy' and 'Mono' in family:continue
                if matched['id'] in ('pingfang','heiti') and not (' SC' in family or 'SC' in family):continue
                if matched['id']=='sourcehan' and any(t in family for t in (' HW',' JP',' KR',' TC',' HC')):continue
                if matched['id']=='harmony' and ' SC' not in family and '_SC_' not in path.name and 'SansSC' not in path.name:continue
                if 'italic' in style.lower() or 'oblique' in style.lower():continue
                weight=int(font['OS/2'].usWeightClass) if 'OS/2' in font else 400
                axes={a.axisTag:(a.minValue,a.defaultValue,a.maxValue) for a in font['fvar'].axes} if 'fvar' in font else {}
                weights=[w for w in (400,500,700) if axes['wght'][0]<=w<=axes['wght'][2]] if 'wght' in axes else [weight]
            for w in weights:
                if w not in (400,500,700):continue
                key=(matched['id'],w)
                if key in self.faces:continue
                s=Source(matched['id'],path,index,{'wght':w} if 'wght' in axes else None)
                missing=s.missing(''.join(line for lines in TEXTS.values() for line in lines)+'清晨光线慢慢移来')
                if missing:
                    self.errors.append({'file':path.name,'error':'样文缺字：'+''.join(missing)[:100]});continue
                self.faces[key]=s
    def get(self,font,weight):
        try:return self.faces[(font,weight)]
        except KeyError:raise ValueError(f'{font} 的 {weight} 字重尚未载入；不会用其他字体顶替') from None
    def public(self):
        return [{k:v for k,v in c.items() if k not in ('match','downloads')} |
                {'available':any(f==c['id'] for f,w in self.faces),
                 'weights':sorted(w for f,w in self.faces if f==c['id']),
                 'faces':[s.info() for (f,w),s in sorted(self.faces.items()) if f==c['id']],
                 'can_download':bool(c['downloads'])} for c in CATALOG]
    def download(self,font):
        c=next((x for x in CATALOG if x['id']==font),None)
        if not c:raise ValueError('未知字体')
        if not c['downloads']:raise ValueError('此字体请使用官网获取的文件；可将文件放进 fonts 文件夹后点重新扫描')
        dest=self.cache/'fonts';dest.mkdir(parents=True,exist_ok=True)
        receipts=[]
        for url,name in c['downloads']:
            target=dest/name
            if target.exists():continue
            with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'FontBench/5.0'}),timeout=45) as response:
                raw=response.read(100*1024*1024+1)
            if len(raw)>100*1024*1024 or raw[:4] not in (b'OTTO',b'\0\1\0\0',b'ttcf'):raise ValueError('下载结果不是可识别的字体文件')
            with tempfile.NamedTemporaryFile(dir=dest,delete=False) as f:f.write(raw);tmp=Path(f.name)
            try:
                with TTFont(tmp,lazy=True) as ft:
                    if not ft.getBestCmap():raise ValueError('字体字符表为空')
                os.replace(tmp,target)
            finally:tmp.unlink(missing_ok=True)
            receipts.append({'url':url,'file':name,'sha256':hashlib.sha256(raw).hexdigest()})
        (self.cache/f'download-{font}.json').write_text(json.dumps(receipts,ensure_ascii=False,indent=2))
        self.refresh();return receipts
