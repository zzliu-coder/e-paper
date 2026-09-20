import importlib.util
from pathlib import Path
import pytest

spec=importlib.util.spec_from_file_location('resource_manifest',Path(__file__).parents[1]/'host/paper_resource_manifest.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def test_resource_identity_missing_corrupt(tmp_path):
    p=tmp_path/'paper/fonts/a.pgf';p.parent.mkdir(parents=True);p.write_bytes(b'font')
    manifest={'schema':1,'files':[{'path':'paper/fonts/a.pgf','size':4,'sha256':module.digest(p)}]}
    assert module.verify(tmp_path,manifest)==[]
    p.write_bytes(b'bad!');assert module.verify(tmp_path,manifest)[0]['error']=='mismatch'
    p.unlink();assert module.verify(tmp_path,manifest)[0]['error']=='missing'

@pytest.mark.parametrize('path',['../secret','/secret','books/book.epub'])
def test_unsafe_path(tmp_path,path):
    with pytest.raises(ValueError):module.verify(tmp_path,{'schema':1,'files':[{'path':path,'size':0,'sha256':''}]})

def test_external_symlink(tmp_path):
    (tmp_path/'paper').symlink_to(tmp_path.parent,target_is_directory=True)
    with pytest.raises(ValueError):module.verify(tmp_path,{'schema':1,'files':[{'path':'paper/secret','size':0,'sha256':''}]})
