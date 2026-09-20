"""A8 -> logical luminance -> SSD1677 selector planes, independent of the bus.

No waveform bytes or physical-quality claims are inferred from a browser preview.
The overlay selector is NOT the binary representation of luminance. Source:
CrossMux mapTwoBitPixel (Metalio), FreeInk SDK 094976e1d47ad7120cf461fec5f6b737eaabf13f.
"""
from __future__ import annotations
from .model import Spec

PROFILE = 'GDEM0397T81-freeink-094976e1-overlay-v1'
W, H = 800, 480

def coverage_to_luma(raw: bytes, spec: Spec) -> bytes:
    # Match C++ integer rounding. Gray uses the identical A8 mother sample;
    # tone is deliberately inapplicable in the first grayscale experiment.
    out=bytearray(len(raw))
    levels=(1<<spec.input_bpp)-1
    for i,v in enumerate(raw):
        if spec.input_bpp!=8:
            v=(((v*levels+127)//255)*255+levels//2)//levels
        if spec.output=='gray4':
            coverage=(v*3+127)//255
            out[i]=coverage*85 if spec.inverse else (3-coverage)*85
        else:
            ink=v>=spec.threshold
            out[i]=255 if (ink if spec.inverse else not ink) else 0
    return bytes(out)

def selector(luma: int) -> tuple[int,int,int]:
    """base (1=white), LSB (BW RAM), MSB (RED RAM)."""
    if luma not in (0,85,170,255):raise ValueError('Expected exact four-level luminance')
    level=luma//85
    return int(level==3),int(level==1),int(level in (1,2))

def portrait_to_native(luma: bytes) -> bytes:
    if len(luma)!=480*800:raise ValueError('Expected portrait 480×800')
    out=bytearray(len(luma))
    for y in range(800):
        for x in range(480):out[(479-x)*800+y]=luma[y*480+x]
    return bytes(out)

def planes(luma: bytes, width=800, height=480) -> tuple[bytes,bytes,bytes]:
    if width%8 or len(luma)!=width*height:raise ValueError('Invalid dimensions')
    out=[bytearray(len(luma)//8) for _ in range(3)]
    for i,v in enumerate(luma):
        for p,b in zip(out,selector(v)):
            if b:p[i//8]|=128>>(i%8)
    return tuple(bytes(p) for p in out)

def calibration(width=480,height=800) -> bytes:
    """Reference four solid blocks, monochrome corner markers. No dithering."""
    out=bytearray([255])*(width*height)
    for block,value in enumerate((255,170,85,0)):
        x0=24+(block%2)*228;y0=128+(block//2)*252
        for y in range(y0,min(y0+210,height-24)):
            for x in range(x0,min(x0+204,width-24)):out[y*width+x]=value
    for y in range(8,16):
        for x in range(8,16):out[y*width+x]=0
    return bytes(out)
