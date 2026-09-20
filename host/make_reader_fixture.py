"""Generate a small, deterministic EPUB with CSS and an illustration (no downloads)."""
import argparse
import struct
import zlib
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED


def illustration():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    w, h = 320, 200
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw.append(0 if x < 4 or y < 4 or x >= w-4 or y >= h-4 else
                       (255 if (x//32+y//25) % 2 else x*255//w))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w,h,8,0,0,0,0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


def build(path):
    files = {
        'mimetype': b'application/epub+zip',
        'META-INF/container.xml': b'<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',
        'OPS/book.opf': '''<package><metadata><title>阅读链路验收</title><creator>SDK 本地测试</creator></metadata><manifest>
<item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="s" href="style.css" media-type="text/css"/>
<item id="i" href="test.png" media-type="image/png"/></manifest><spine><itemref idref="c"/></spine></package>'''.encode(),
        'OPS/style.css': b'h1 {text-align:center;} .right {text-align:right;}',
        'OPS/chapter.xhtml': '''<html><head><link rel="stylesheet" href="style.css"/></head><body>
<h1>阅读链路验收</h1><p>这一页检查中文、标题居中、书内样式和插图。</p>
<p>普通文字与<span style="text-decoration:underline">局部下划线</span>文字。</p>
<p class="right">这一段靠右排列。</p><img src="test.png" alt="黑白网格与灰度渐变"/>
<p>这是插图后的文字。请回翻，确认上一页仍是插图，再回翻到标题。</p></body></html>'''.encode(),
        'OPS/test.png': illustration(),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(path, 'x', compression=ZIP_DEFLATED) as book:
        for name, data in files.items():
            book.writestr(name, data, compress_type=0 if name == 'mimetype' else ZIP_DEFLATED)
    return path


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('output',type=Path)
    print(build(p.parse_args().output))
