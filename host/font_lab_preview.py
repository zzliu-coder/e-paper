"""Export and compare lossless PNGs from the native LVGL I1 renderer.

The script measures pixel differences only. It never turns a pixel count into
a claim about human readability; that part still needs a real-panel review.
"""
from pathlib import Path
from PIL import Image, ImageChops
import itertools
import json
import argparse

root = Path(__file__).resolve().parents[1]
dest = root / 'artifacts/font-lab3.1/host-renders'

parser = argparse.ArgumentParser(description='Compare host renders with captured device frames')
parser.add_argument('--runtime', type=Path,
                    default=root / 'artifacts/font-lab3.1/runtime-rerun',
                    help='directory containing device page captures')
args = parser.parse_args()

for profile in range(4):
    name = f'lab-{profile}-0-3'
    with Image.open(dest / (name + '.pbm')) as image:
        image.save(dest / (name + '.png'))

with Image.open(dest / 'lab-2-0-3.pbm') as image:
    image.save(root / 'artifacts/font-lab3.1/preview-screen.png')

report = {'profiles': {
    '0': 'A / Source Han Sans Medium / Pillow baseline / MONO output',
    '1': 'B / Source Han Sans Medium / lv_font_conv MONO',
    '2': 'C / LXGW Neo XiHei Screen / lv_font_conv MONO',
    '3': 'D / WenQuanYi Micro Hei / lv_font_conv MONO',
}, 'fontpack_page': {
    '2bpp': 'existing font_data fontpack, mapped device sizes 18/25/28/30/36',
    '4bpp': 'existing font_data fontpack, 30 px mask',
    'panel_output': '1-bit I1; no true grayscale waveform'
}, 'glyph_differences': {}, 'device_frame_matches': {}}

for index, size in enumerate((16, 18, 20, 22, 24, 28, 32, 40)):
    comparisons = {}
    for left, right in itertools.combinations(range(4), 2):
        with Image.open(dest / f'lab-{left}-2-{index}.pbm') as a, Image.open(dest / f'lab-{right}-2-{index}.pbm') as b:
            diff = ImageChops.difference(a.crop((36, 294, 444, 346)), b.crop((36, 294, 444, 346))).convert('L')
            comparisons[f'{left}/{right}'] = sum(value != 0 for value in diff.tobytes())
    report['glyph_differences'][str(size)] = comparisons

runtime = args.runtime
for page in range(5):
    path = runtime / f'page-{page}.png'
    host_path = dest / f'lab-0-{page}-3.pbm'
    if path.exists() and host_path.exists():
        with Image.open(path) as device, Image.open(host_path) as host:
            same = ImageChops.difference(device, host).getbbox() is None
            report['device_frame_matches'][str(page)] = same
            assert same, ('host/device frame mismatch', page)

(root / 'artifacts/font-lab3.1/render-comparison.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n'
)
print(json.dumps(report, ensure_ascii=False, indent=2))
