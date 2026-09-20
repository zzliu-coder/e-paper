#!/usr/bin/env python3
"""Check/build the installed experiment in an existing ESP-IDF environment; never flash."""
from pathlib import Path
import argparse,json,shutil,subprocess,hashlib,sys

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=Path,required=True);p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--build',action='store_true');a=p.parse_args()
    repo=a.repo.expanduser().resolve();build=a.build_dir.expanduser().resolve()
    if not (repo/'CMakeLists.txt').is_file():p.error('仓库缺少 CMakeLists.txt')
    receipt=repo/'.fontbench-install.json'
    if not receipt.is_file():p.error('先完成本版源码合入')
    doc=json.loads(receipt.read_text())
    if doc.get('version')!='1.0.0-fontbench6':p.error('合入回执版本不匹配')
    for rel,sha in doc['installed'].items():
        target=(repo/rel).resolve()
        if repo not in target.parents:p.error('回执路径异常')
        if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest()!=sha:p.error('合入后文件有变化：'+rel)
    idf=shutil.which('idf.py')
    report={'toolchain_available':bool(idf),'source_receipt':'PASS','full_build':'NOT_RUN','flash':False,'physical_quality':'NOT_PROVEN'}
    if not a.build:print(json.dumps(report,ensure_ascii=False,indent=2));return
    if not idf:p.error('请先激活现有 ESP-IDF 环境；没有自动安装第二套工具链')
    if build==repo or repo in build.parents and build.name in ('main','components','host'):p.error('请用独立构建目录')
    if build.exists() and (build/'fontbench-build-check.json').exists():p.error('该目录已有本工具回执，请换一个构建目录')
    build.mkdir(parents=True,exist_ok=True)
    cmd=[idf,'-B',str(build),'build'];report['command']=cmd
    with (build/'fontbench-build.log').open('w') as log:
        run=subprocess.run(cmd,cwd=repo,stdout=log,stderr=subprocess.STDOUT)
    report['returncode']=run.returncode;report['full_build']='PASS' if run.returncode==0 else 'FAIL'
    report['binaries']={x.name:{'bytes':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in build.glob('*.bin')}
    if run.returncode==0 and not report['binaries']:report['full_build']='FAIL';report['reason']='build returned success without top-level app binary'
    (build/'fontbench-build-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2));print(json.dumps(report,ensure_ascii=False,indent=2))
    if report['full_build']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
