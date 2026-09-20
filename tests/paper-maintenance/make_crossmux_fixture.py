"""Generated source-only fixture; no personal books or device writes."""
from pathlib import Path
from io import BytesIO
from zipfile import ZipFile,ZIP_DEFLATED
from PIL import Image
import sys

target=Path(sys.argv[1])
files={
 'mimetype':b'application/epub+zip',
 'META-INF/container.xml':b'<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',
 'OPS/book.opf':b'<package><metadata><title>CrossMux image fixture</title></metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>',
 'OPS/c.xhtml':'<html><body><p>PNG 透明图像</p><img src="alpha.png"/><p>JPEG 灰度图像</p><img src="gray.jpg"/><p>END 结束</p></body></html>'.encode(),
}
for ext,mode in [('png','RGBA'),('jpg','RGB')]:
    image=Image.new(mode,(320,180))
    for y in range(180):
        for x in range(320):
            v=0 if (x//20+y//20)%2 else 255
            image.putpixel((x,y),(v,v,v,0 if x<80 else 255) if mode=='RGBA' else (v,v,v))
    stream=BytesIO();image.save(stream,format='PNG' if ext=='png' else 'JPEG')
    files['OPS/'+('alpha.png' if ext=='png' else 'gray.jpg')]=stream.getvalue()
with ZipFile(target,'x',compression=ZIP_DEFLATED) as archive:
    for name,data in files.items():archive.writestr(name,data,compress_type=0 if name=='mimetype' else ZIP_DEFLATED)
print(target)
