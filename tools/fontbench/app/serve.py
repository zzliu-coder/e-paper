#!/usr/bin/env python3
"""Local-only Font Bench server. All remote/device actions require an explicit button."""
from __future__ import annotations
import argparse,base64,functools,json,os,secrets,sys,threading,time,urllib.parse,webbrowser,zipfile
from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
from fontbench.model import Spec
from fontbench.sources import Registry,CATALOG
from fontbench.render import Engine
from fontbench.bank import build,controls
from fontbench import bridge

def make_server(port=8766,repo=None,extra=None,system=True):
    token=secrets.token_urlsafe(24);lock=threading.RLock()
    cache=Path.home()/'.cache/epaper-fontbench'
    registry=Registry(repo,cache,extra,system=system,prebuilt=ROOT/'sdcard/inkdesk/fontbench');engine=Engine(registry)
    generated=None
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self,*a,**kw):super().__init__(*a,directory=str(ROOT/'web'),**kw)
        def log_message(self,*a):pass
        def send_json(self,obj,status=200):
            raw=json.dumps(obj,ensure_ascii=False).encode();self.send_response(status)
            self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
        def host_ok(self):
            return self.headers.get('Host') in (f'127.0.0.1:{self.server.server_port}',f'localhost:{self.server.server_port}')
        def do_GET(self):
            if not self.host_ok():return self.send_json({'error':'本地地址校验失败'},403)
            path=urllib.parse.urlsplit(self.path).path
            if path=='/api/bootstrap':
                with lock:return self.send_json({'token':token,'catalog':registry.public(),'controls':controls(),'repo':str(registry.repo or ''),'errors':registry.errors,'mode':'local'})
            if path=='/api/sd-download':
                if generated is None:return self.send_json({'error':'请先生成 SD 资源'},404)
                self.send_response(200);self.send_header('Content-Type','application/zip');self.send_header('Content-Disposition','attachment; filename="fontbench-sd.zip"');self.send_header('Content-Length',str(generated.stat().st_size));self.end_headers()
                with generated.open('rb') as f:
                    while b:=f.read(65536):self.wfile.write(b)
                return
            if path.startswith('/api/'):return self.send_json({'error':'未知接口'},404)
            return super().do_GET()
        def do_POST(self):
            nonlocal registry,engine,generated
            if not self.host_ok() or self.headers.get('X-FontBench-Token')!=token:return self.send_json({'error':'本地操作令牌校验失败'},403)
            origin=self.headers.get('Origin')
            if origin and origin not in (f'http://127.0.0.1:{self.server.server_port}',f'http://localhost:{self.server.server_port}'):
                return self.send_json({'error':'跨站请求已阻止'},403)
            try:
                n=int(self.headers.get('Content-Length','0'))
                if not 0<n<=65536:raise ValueError('请求大小无效')
                args=json.loads(self.rfile.read(n))
                if not isinstance(args,dict):raise ValueError('请求须为对象')
                path=urllib.parse.urlsplit(self.path).path
                with lock:
                    if path=='/api/render':
                        kind=args.get('kind','compact')
                        if kind not in ('compact','wide'):raise ValueError('样本格式无效')
                        png,meta=engine.png(Spec.parse(args['spec']),kind)
                        return self.send_json({'png':base64.b64encode(png).decode(),'meta':meta})
                    if path=='/api/scan':
                        repo=args.get('repo') or None;extra=args.get('extra') or None
                        for p in (repo,extra):
                            if p and not Path(p).expanduser().exists():raise ValueError('目录不存在：'+p)
                        new=Registry(repo,cache,[extra] if extra else [],prebuilt=ROOT/'sdcard/inkdesk/fontbench')
                        registry=new;engine=Engine(registry)
                        return self.send_json({'catalog':registry.public(),'errors':registry.errors})
                    if path=='/api/download-fonts':
                        results={}
                        for c in CATALOG:
                            if c['downloads']:
                                try:results[c['id']]={'downloads':registry.download(c['id'])}
                                except Exception as ex:results[c['id']]={'error':str(ex)}
                        engine.clear();return self.send_json({'results':results,'catalog':registry.public()})
                    if path=='/api/sd':
                        dest=cache/'exports'/('bank-'+secrets.token_hex(4));dest.parent.mkdir(parents=True,exist_ok=True)
                        manifest,_=build(engine,dest);generated=dest.with_suffix('.zip')
                        with zipfile.ZipFile(generated,'w',zipfile.ZIP_DEFLATED) as z:
                            for f in sorted(dest.rglob('*')):
                                if f.is_file():z.write(f,'inkdesk/fontbench/'+f.relative_to(dest).as_posix())
                        return self.send_json({'tiles':manifest['tile_count'],'url':'/api/sd-download','path':str(generated),'fonts':[c['name'] for c in manifest['catalog'] if c['available']]})
                    if path=='/api/device':return self.send_json({'hello':bridge.call('hello'),'state':bridge.state()})
                    if path=='/api/device-apply':return self.send_json(bridge.apply(args['spec'],args.get('axis',2),args.get('values'),args.get('pin')))
                    if path=='/api/device-control':return self.send_json(bridge.control(args['field'],args['value']))
                    if path=='/api/device-logs':return self.send_json(bridge.logs())
                self.send_json({'error':'未知接口'},404)
            except (OSError,ValueError,KeyError,RuntimeError,TimeoutError) as ex:self.send_json({'error':str(ex)},400)
    return ThreadingHTTPServer(('127.0.0.1',port),Handler)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--repo');p.add_argument('--fonts',action='append',default=[]);p.add_argument('--port',type=int,default=8766);p.add_argument('--no-browser',action='store_true');a=p.parse_args()
    server=make_server(a.port,a.repo,a.fonts);url=f'http://127.0.0.1:{server.server_port}'
    print('字体试验台：'+url,flush=True)
    if not a.no_browser:webbrowser.open(url)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
