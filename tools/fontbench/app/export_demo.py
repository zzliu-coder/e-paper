"""Build an offline HTML with exact sample pictures, no fonts."""
from pathlib import Path
import base64,json
root=Path(__file__).resolve().parents[1];bank=root/'sdcard/inkdesk/fontbench'
doc={'manifest':json.loads((bank/'manifest.json').read_text()),'controls':base64.b64encode((bank/'controls.i1').read_bytes()).decode(),
     'tiles':{f.name:base64.b64encode(f.read_bytes()).decode() for f in sorted((bank/'tiles').glob('*.t5'))}}
js='window.FONTBENCH_DEMO='+json.dumps(doc,ensure_ascii=False,separators=(',',':'))+';\n'
(root/'web/demo-data.js').write_text(js)
html=(root/'web/index.html').read_text().replace('<link rel="stylesheet" href="style.css">','<style>'+(root/'web/style.css').read_text()+'</style>').replace('<script src="demo-data.js"></script>','<script>'+js+'</script>').replace('<script src="app.js"></script>','<script>'+(root/'web/app.js').read_text()+'</script>')
(root/'字体试验台-直接打开.html').write_text(html)
print('Offline HTML ready:',len(html.encode()),'bytes, sample images only')
