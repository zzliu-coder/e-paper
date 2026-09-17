#!/usr/bin/env python3
"""Create a reviewable candidate manifest; never flashes."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil

root=Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("--output-dir", type=Path, default=root / "artifacts")
parser.add_argument("--plan-name", default="install-plan.json")
parser.add_argument("--candidate", help="Override the version recorded in the plan")
args = parser.parse_args()
build=args.build.resolve()
out=args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
description = {}
description_path = build / "project_description.json"
if description_path.exists():
    description = json.loads(description_path.read_text())
candidate = args.candidate or description.get("project_version", "unknown")
segments=[]
for line in (build/"flash_args").read_text().splitlines():
    parts=line.split()
    if len(parts)!=2 or not parts[0].startswith("0x"):
        continue
    offset=int(parts[0],16)
    source=(build/parts[1]).resolve()
    data=source.read_bytes()
    target=out/source.name
    shutil.copyfile(source,target)
    segments.append({"file":str(target),"offset":hex(offset),"size":len(data),
                     "sha256":hashlib.sha256(data).hexdigest(),
                     "erase_end_exclusive":hex((offset+len(data)+4095)//4096*4096)})
segments.sort(key=lambda s:int(s["offset"],16))
for a,b in zip(segments,segments[1:]):
    if int(a["erase_end_exclusive"],16)>int(b["offset"],16):
        raise RuntimeError("Overlapping sectors")
if len(segments)!=7:
    raise RuntimeError("Expected the seven official image segments")
plan={"candidate":candidate,"executed":False,"approval_required":False,
      "device_mac":"10:20:ba:6e:0b:e0","source_official_commit":"6d05a6583c0feaea97b91d9aade3ce6757321dc4",
      "idf":"5.5.4","segments":segments,
      "write_segments":[s for s in segments if int(s["offset"],16)==0x80000],
      "preconditions":"Before any write, verify live device identity, current partition table and app0 selection match the user-verified official baseline. On mismatch stop and revise plan.",
      "recovery_to_verified_official":{
          "file":str(root.parent/"metalio-official-build-2026-09-15"/"xiaozhi.bin"),
          "offset":"0x80000","size":4082800,
          "sha256":"695994ce4e109775c99d4eb4cbde583bc579bc5642e908bcec74c89b0ec9f769"},
      "recovery":"Original 16MiB image SHA256 5b7986151b18163f9087611edfedd563c49e37c32c12d999d0e0fa3d82a1cfaf at offset 0; separate approval required.",
      "warning":"This replaces the user-verified official product firmware with an experimental diagnostic build. All hardware runtime results are NOT_PROVEN."}
(out/args.plan_name).write_text(json.dumps(plan,indent=2)+"\n")
print(json.dumps(plan,indent=2))
