"""Independent Pillow reference, all four IDCT scales and chroma subsamplings."""
from pathlib import Path
import argparse,subprocess,json
from PIL import Image,ImageDraw,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('probe');p.add_argument('out',type=Path);p.add_argument('images',nargs='*',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
files=list(a.images)
for subsampling in (0,1,2):
    im=Image.new('RGB',(799,748),'white');d=ImageDraw.Draw(im)
    for y in range(0,748,16):
        for x in range(0,799,16):
            g=((x//16+y//16)%4)*85;d.rectangle((x,y,x+15,y+15),fill=(g,g,g))
    f=a.out/f'synthetic-{subsampling}.jpg';im.save(f,quality=95,subsampling=subsampling);files.append(f)
results=[]
for index,f in enumerate(files):
    im=Image.open(f).convert('L')
    for scale in range(4):
        output=a.out/f'{index}-{scale}.pgm';subprocess.run([a.probe,str(f),str(output),str(scale)],check=True)
        got=Image.open(output);step=1<<scale
        ref=im.crop((0,0,got.width*step,got.height*step)).resize(got.size,Image.Resampling.BOX)
        mae=ImageStat.Stat(ImageChops.difference(got,ref)).mean[0]
        assert mae<4,(f,scale,mae)
        results.append(dict(file=f.name,scale=scale,mae=mae,mean=ImageStat.Stat(got).mean[0]))
(a.out/'results.json').write_text(json.dumps(results,indent=2))
print(f'PASS {len(results)} pixel-reference comparisons; worst MAE {max(r["mae"] for r in results):.3f}')
