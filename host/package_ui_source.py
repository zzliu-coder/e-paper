"""Snapshot the UI change set and source hashes, not a duplicate SDK/toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('release',type=Path);a=p.parse_args()
files=[root/n for n in ('CMakeLists.txt','main/CMakeLists.txt','main/ui_demo.cc','main/ui_demo.h',
 'main/ui_demo_fonts.c','main/inkdesk_app.cc','main/personal_sdk.cc','main/inkdesk_r2/core/types.h',
 'host/build_ui_fonts.py','host/render_ui_frames.py','host/ui_acceptance.py','host/test_ui_native.py',
 'host/package_ui_source.py','tests/ui_demo_test.cpp','docs/UI_COMPONENTS.md','docs/DESIGN_SYSTEM.md',
 'use_font/ui-themes/build-manifest.json','use_font/ui-themes/README.md','dependencies.lock','sdkconfig')]
files+=list((root/'main/ui').glob('*.h'))+list((root/'tests/lvgl_host').glob('*'))
files+=list((root/'main/font_lab_assets').glob('*.c'))
files+=[root/n for n in ('host/build_font_lab.py','host/font_lab_preview.py','host/font_lab_acceptance.py',
 'host/font_lab_export.py','host/font_lab_open.py','host/metalio.py','tests/test_font_lab_export.py','docs/FONT_LAB.md','use_font/ui-themes/font-lab-manifest.json')]
files+=list((root/'use_font/ui-themes/candidates').glob('*'))
files=sorted(set(f for f in files if f.is_file()))
report={'purpose':'UI change-set snapshot; source fonts and full base project remain in SDK',
 'files':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
a.release.mkdir(parents=True,exist_ok=True)
(a.release/'source-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
with tarfile.open(a.release/'ui-source.tar.gz','w:gz') as archive:
    for f in files:archive.add(f,arcname=str(f.relative_to(root)))
print('Archived',len(files),'source/config/test/doc files')
