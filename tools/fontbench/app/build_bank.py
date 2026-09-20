#!/usr/bin/env python3
from pathlib import Path
import argparse
from fontbench.sources import Registry
from fontbench.render import Engine
from fontbench.bank import build
p=argparse.ArgumentParser(description='生成本机已载入字体的精确 SD 试字图片；不分发字体文件')
p.add_argument('--repo');p.add_argument('--fonts',action='append',default=[]);p.add_argument('--out',required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1]
r=Registry(a.repo,extra=a.fonts,prebuilt=root/'sdcard/inkdesk/fontbench')
manifest,_=build(Engine(r),a.out,lambda n:print(f'已生成 {n} 个参数样本',flush=True))
print('完成',manifest['tile_count'],'个样本图片。仅复制到 SD 的 inkdesk/fontbench，不刷写字体分区。')
