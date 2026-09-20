"""Lossless provenance import; incomplete historical scores never become current votes."""
from __future__ import annotations
import hashlib,json
from .model import canonical,Spec

def rows(data):
    if not isinstance(data,str) or len(data.encode())>4*1024*1024:raise ValueError('最多导入 4 MB 记录')
    try:
        doc=json.loads(data)
        items=doc if isinstance(doc,list) else doc.get('records',[doc]) if isinstance(doc,dict) else []
    except json.JSONDecodeError:
        items=[json.loads(x) for x in data.splitlines() if x.strip()]
    if len(items)>5000:raise ValueError('记录超过 5000 条，请分批导入')
    out=[]
    for r in items:
        if not isinstance(r,dict):raise ValueError('每条记录应为 JSON 对象')
        origin='fontbench' if r.get('schema') in (5,6) else 'lab4' if r.get('schema')==4 or 'sample_id' in r else 'lab3'
        signature=hashlib.sha256(canonical(r)).hexdigest()
        eligible=origin=='fontbench' and r.get('resolved_ok') is True and bool(r.get('sample_id'))
        if eligible:
            try:Spec.parse(r['spec'])
            except (ValueError,KeyError,TypeError):eligible=False
        out.append({'import_id':signature,'origin':origin,'eligible':eligible,'original':r})
    return out

def merge(existing,incoming):
    out=list(existing);seen={r['import_id'] for r in out}
    for r in incoming:
        if r['import_id'] not in seen:out.append(r);seen.add(r['import_id'])
    return out
