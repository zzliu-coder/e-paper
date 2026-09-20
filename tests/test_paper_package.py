import hashlib,importlib.util,json,struct,sys
from pathlib import Path
import pytest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from paper_dev import package

def image(tmp_path,*,chip=9,project=b'xiaozhi',version=b'1.0.0-paper-test'):
    data=bytearray(1024);data[0]=0xe9;struct.pack_into('<H',data,12,chip)
    struct.pack_into('<I',data,32,0xabcd5432)
    data[48:48+len(version)]=version;data[80:80+len(project)]=project
    path=tmp_path/'candidate.bin';path.write_bytes(data);return path

def test_package_manifest_and_readback(tmp_path):
    src=image(tmp_path);out=tmp_path/'release';m=package(src,out)
    assert m['board']=='metalio_eink4' and m['layout']=='metalio-16m-v1-5m'
    assert m['sha256']==hashlib.sha256(src.read_bytes()).hexdigest()
    assert (out/'update.bin').read_bytes()==src.read_bytes()
    assert json.loads((out/'update.json').read_text())==m

@pytest.mark.parametrize('kwargs',[dict(chip=0),dict(project=b'other'),dict(version=b'foreign')])
def test_rejects_other_board_project_or_version(tmp_path,kwargs):
    with pytest.raises(ValueError):package(image(tmp_path,**kwargs),tmp_path/'release')
    assert not (tmp_path/'release').exists()

def test_existing_release_is_preserved(tmp_path):
    src=image(tmp_path);out=tmp_path/'release';package(src,out);old=(out/'update.bin').read_bytes()
    with pytest.raises(FileExistsError):package(src,out)
    assert (out/'update.bin').read_bytes()==old

def test_full_flash_image_is_rejected(tmp_path):
    src=tmp_path/'merged.bin';src.write_bytes(b'\xff'*(6*1024*1024))
    with pytest.raises(ValueError):package(src,tmp_path/'release')
