#!/usr/bin/env python3
"""Open the unified panel; --page is accepted only as a legacy compatibility argument."""
import argparse
from fontbench import bridge
p=argparse.ArgumentParser();p.add_argument('--page',type=int,default=None);a=p.parse_args()
if a.page is not None:print('旧页码已忽略；字体、字号和算法现在直接在统一面板选择。')
b=bridge.state()['fontbench'].get('frame_revision',0)
bridge.call('inkdesk.fontbench.open');r=bridge.wait(after_frame=b)
print('统一字体试验台已打开；实际资源包：',r['fontbench']['bundle_tag'])
