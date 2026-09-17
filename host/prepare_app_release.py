"""Package an app-only release and explicit rollback plan; does not touch device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("version")
p.add_argument("--build",type=Path,required=True)
p.add_argument("--previous",required=True)
a=p.parse_args()
dest=root/"artifacts"/a.version
dest.mkdir(parents=True,exist_ok=True)
app=a.build/"xiaozhi.bin"
data=app.read_bytes()
assert 0<len(data)<=0x500000
target=dest/"xiaozhi.bin"
if target.exists():raise FileExistsError(target)
shutil.copy2(app,target)
previous=root/"artifacts"/a.previous/"xiaozhi.bin"
rollback=previous.read_bytes()
segment={"file":str(target),"offset":"0x80000","size":len(data),"sha256":hashlib.sha256(data).hexdigest(),
         "erase_end_exclusive":hex(0x80000+((len(data)+4095)//4096)*4096)}
candidate_version=a.version.replace("font-lab","fontlab")
plan={"candidate":"1.0.0-"+candidate_version,"executed":False,"device_mac":"10:20:ba:6e:0b:e0",
      "write_segments":[segment],"excluded":["bootloader","partition table","otadata","NVS","fonts","SD","eFuse"],
      "preconditions":"Live identity, security disabled, partition hash and ota_0 selection verified before writing",
      "rollback":{"file":str(previous),"offset":"0x80000","sha256":hashlib.sha256(rollback).hexdigest(),"size":len(rollback)},
      "full_recovery":"Original full backup retained; full-flash restore requires separate authorization"}
(dest/"install-plan.json").write_text(json.dumps(plan,indent=2)+"\n")
print(json.dumps(plan,indent=2))
