"""Freeze a reader overlay, build inputs and checksums. Offline; never opens USB."""
import argparse,hashlib,json,subprocess,tarfile
from pathlib import Path
from paper_recovery_check import check_app

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release",type=Path)
    parser.add_argument("--build",type=Path,required=True)
    args=parser.parse_args();sdk=Path(__file__).resolve().parents[1]
    release=args.release.resolve();image=(release/"update.bin").read_bytes()
    manifest=json.loads((release/"update.json").read_text())
    assert hashlib.sha256(image).hexdigest()==manifest["sha256"]
    assert len(image)==manifest["size"] and len(image)<=0x500000
    assert image==(args.build/"xiaozhi.bin").read_bytes()
    checked=check_app(image,0,len(image));assert checked["image_bytes"]==len(image)
    files=[]
    for name in ("components/paper_core","components/paper_reader_crossmux","components/esp-wifi-connect","main/paper_shell",
                 "tests/paper-maintenance","host","docs","design-tokens"):
        files += [p for p in (sdk/name).rglob("*") if p.is_file() and not p.is_symlink()
                  and (p.suffix in (".c",".h",".cc",".cpp",".hpp",".inc",".cmake",".py",".md",".json",".txt",".t") or p.name=="Kconfig.projbuild" or p.name.startswith(("LICENSE","NOTICE")))
                  and "__pycache__" not in p.parts]
    for name in ("LICENSE","CMakeLists.txt","sdkconfig","dependencies.lock","main/CMakeLists.txt",
                 "main/personal_sdk.cc","main/boards/common/power_policy/power_hw.cc",
                 "main/boards/common/bt_audio_codec.cc","main/boards/common/bt_audio_codec.h",
                 "tools/paper/build_font_proofs.py",
                 "main/display/lv_adapter_display.cc","main/display/lv_adapter_display.h",
                 "tools/audit_crossmux_vendor.py","tools/generate_ui_tokens.py"):
        file=sdk/name
        if file.is_file():files.append(file)
    files=sorted(set(files))
    record={"scope":"reader source overlay; not a standalone SDK/toolchain or installed device",
            "git_head":subprocess.check_output(["git","-C",str(sdk),"rev-parse","HEAD"],text=True).strip(),
            "working_tree_dirty":True,"image":manifest,"image_integrity":checked,
            "app_slot_bytes":0x500000,"app_slot_free":0x500000-len(image),
            "source_files":{str(p.relative_to(sdk)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
            "build_files":{n:hashlib.sha256((args.build/n).read_bytes()).hexdigest() for n in
                ("config/sdkconfig.h","partition_table/partition-table.bin","bootloader/bootloader.bin")}}
    resources=release/'resources.json'
    if resources.exists():
        resource_manifest=json.loads(resources.read_text())
        assert resource_manifest['schema']==1 and resource_manifest['version']==manifest['version']
        record['resources']={'manifest':'resources.json','sha256':hashlib.sha256(resources.read_bytes()).hexdigest(),
                             'files':len(resource_manifest['files']),'device_verification':'separate receipt required'}
    with (release/"source-manifest.json").open("x") as f:json.dump(record,f,ensure_ascii=False,indent=2);f.write("\n")
    with tarfile.open(release/"reader-overlay.tar.gz","x:gz") as archive:
        for file in files:archive.add(file,arcname=str(file.relative_to(sdk)),recursive=False)
    print(json.dumps({"source_files":len(files),"integrity":checked,"remaining_bytes":record["app_slot_free"]}))
if __name__=="__main__":main()
