import importlib.util
from pathlib import Path
import pytest

path=Path(__file__).resolve().parents[1]/'host/font_lab_export.py'
spec=importlib.util.spec_from_file_location('font_lab_export',path)
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)

def reply(chunk,offset,boot='test'):
    return dict(ok=True,device_id='1020ba6e0be0',boot_id=boot,hex=chunk.hex(),bytes=len(chunk),offset=offset)

def test_complete():
    data=b'{"feedback":0}\n'
    assert mod.collect(lambda cmd,offset:reply(data if offset==0 else b'',offset))==data

def test_reboot():
    with pytest.raises(RuntimeError,match='reboot'):
        mod.collect(lambda cmd,offset:reply(b'{}\n' if offset==0 else b'',offset,'a' if offset==0 else 'b'))

def test_bad_offset():
    with pytest.raises(ValueError,match='chunk'):
        mod.collect(lambda cmd,offset:reply(b'',42))

def test_missing_log():
    with pytest.raises(RuntimeError):mod.collect(lambda *args,**kwargs:dict(ok=False,error='log_unavailable'))
