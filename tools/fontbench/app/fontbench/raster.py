"""Explicit FreeType load/render modes and unquantised A8 glyph masters."""
from __future__ import annotations
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
import hashlib
import importlib.metadata
import os
import struct
import ctypes
import freetype as ft
from fontTools.ttLib import TTFont
from PIL import Image

ALGORITHMS = {
    'O-N': (ft.FT_LOAD_NO_HINTING | ft.FT_LOAD_NO_AUTOHINT, ft.FT_RENDER_MODE_NORMAL),
    'M-A': (ft.FT_LOAD_FORCE_AUTOHINT | ft.FT_LOAD_TARGET_MONO, ft.FT_RENDER_MODE_MONO),
    'M-N': (ft.FT_LOAD_NO_AUTOHINT | ft.FT_LOAD_TARGET_MONO, ft.FT_RENDER_MODE_MONO),
    'N-A': (ft.FT_LOAD_FORCE_AUTOHINT | ft.FT_LOAD_TARGET_NORMAL, ft.FT_RENDER_MODE_NORMAL),
    'L-A': (ft.FT_LOAD_FORCE_AUTOHINT | ft.FT_LOAD_TARGET_LIGHT, ft.FT_RENDER_MODE_NORMAL),
    'N-N': (ft.FT_LOAD_NO_AUTOHINT | ft.FT_LOAD_TARGET_NORMAL, ft.FT_RENDER_MODE_NORMAL),
}

# Explicit shared property: do not let an environment-specific CFF darkening default
# silently turn the no-hint baseline into an emboldened sample.
DARKENING_PROPERTIES = {}
for _module in ('cff','autofitter','type1','t1cid'):
    _off=ctypes.c_ubyte(1)
    _err=ft.FT_Property_Set(ft.get_handle(),_module.encode(),b'no-stem-darkening',ctypes.byref(_off))
    DARKENING_PROPERTIES[_module]=int(_err)
if DARKENING_PROPERTIES['cff'] or DARKENING_PROPERTIES['autofitter']:
    raise RuntimeError('FreeType could not explicitly disable CFF/auto stem darkening')

@dataclass(frozen=True)
class Spec:
    font: str
    px: int = 22
    algorithm: str = 'N-A'
    threshold: int = 128
    input_bpp: int = 8
    inverse: bool = False

@dataclass
class Glyph:
    coverage: Image.Image
    left: int
    top: int
    advance: int

class Source:
    def __init__(self, key: str, path: Path, face: int = 0, axes: dict | None = None):
        self.key, self.path, self.face_index = key, Path(path).resolve(), face
        if not self.path.is_file():
            raise FileNotFoundError(self.path)
        self.face = ft.Face(str(self.path), face)
        self.face.select_charmap(ft.FT_ENCODING_UNICODE)
        self.axes = dict(axes or {})
        if self.axes:
            info = self.face.get_variation_info()
            known = {a.tag for a in info.axes}
            if set(self.axes) - known:
                raise ValueError('Unknown variation axis')
            coords = []
            for axis in info.axes:
                val = self.axes.get(axis.tag, axis.default)
                if not axis.minimum <= val <= axis.maximum:
                    raise ValueError('Variation coordinate out of range')
                coords.append(val)
            self.face.set_var_design_coords(coords)
        with TTFont(self.path, fontNumber=face, lazy=True) as font:
            self.cmap = set(font.getBestCmap() or {})
            self.weight = self.axes.get('wght', getattr(font.get('OS/2'), 'usWeightClass', 0))
            # Presence is recorded; it is not a quality claim about the font's hints.
            self.hint_tables = [tag for tag in ('fpgm', 'prep', 'cvt ', 'CFF ', 'CFF2') if tag in font]
        self.family = (self.face.family_name or b'').decode('utf-8', 'replace')
        self.style = (self.face.style_name or b'').decode('utf-8', 'replace')
        self.sha256 = hashlib.sha256(self.path.read_bytes()).hexdigest()

    def info(self):
        return {'id': self.key, 'filename': self.path.name, 'sha256': self.sha256,
                'family': self.family, 'style': self.style, 'face_index': self.face_index,
                'weight': self.weight, 'axes': self.axes, 'hint_tables_present': self.hint_tables}

    def missing(self, text):
        return sorted({c for c in text if c not in '\r\n' and ord(c) not in self.cmap})

    @lru_cache(maxsize=8192)
    def glyph(self, char, px, algorithm):
        if not 16 <= px <= 40 or algorithm not in ALGORITHMS:
            raise ValueError('Unsupported size/mode')
        if ord(char) not in self.cmap or not self.face.get_char_index(ord(char)):
            raise ValueError(f'Missing U+{ord(char):04X} in {self.key}')
        self.face.set_pixel_sizes(0, px)
        flags, render = ALGORITHMS[algorithm]
        self.face.load_char(char, flags | ft.FT_LOAD_NO_BITMAP)
        self.face.glyph.render(render)
        slot = self.face.glyph
        b = slot.bitmap
        w, h, pitch = b.width, b.rows, abs(b.pitch)
        raw = bytes(b.buffer)
        out = bytearray(w*h)
        for y in range(h):
            off = (y if b.pitch >= 0 else h-1-y)*pitch
            for x in range(w):
                if b.pixel_mode == ft.FT_PIXEL_MODE_MONO:
                    v = 255 if raw[off+x//8] & (128 >> (x % 8)) else 0
                elif b.pixel_mode == ft.FT_PIXEL_MODE_GRAY:
                    v = raw[off+x]
                else:
                    raise ValueError(f'Unexpected pixel mode {b.pixel_mode}')
                out[y*w+x] = v
        im = Image.frombytes('L', (w,h), bytes(out)) if w and h else Image.new('L',(0,0))
        return Glyph(im, slot.bitmap_left, slot.bitmap_top, int((slot.advance.x+32)//64))

    def master_hash(self, text, spec):
        digest=hashlib.sha256()
        for char in text:
            if char in '\r\n':
                digest.update(char.encode());continue
            glyph=self.glyph(char,spec.px,spec.algorithm)
            digest.update(char.encode('utf-8'))
            digest.update(struct.pack('<5i',glyph.coverage.width,glyph.coverage.height,glyph.left,glyph.top,glyph.advance))
            digest.update(glyph.coverage.tobytes())
        return digest.hexdigest()

    def width(self, text, spec):
        return sum(self.glyph(c,spec.px,spec.algorithm).advance for c in text)

    def line_metrics(self, text, spec):
        glyphs = [self.glyph(c,spec.px,spec.algorithm) for c in text]
        top = max((g.top for g in glyphs), default=spec.px)
        bottom = min((g.top-g.coverage.height for g in glyphs), default=0)
        return top, max(top-bottom+4, (spec.px*5+3)//4)

    def draw(self, canvas, text, xy, spec):
        """Integer positioning. Word wrapping uses these same hinted advances."""
        if self.missing(text):
            raise ValueError(f'{self.key}: missing {self.missing(text)}')
        x, baseline = xy
        max_width = 0
        for c in text:
            g = self.glyph(c,spec.px,spec.algorithm)
            coverage = quantise(g.coverage, spec.input_bpp)
            ink = coverage.point(lambda v: 255 if v >= spec.threshold else 0)
            if g.coverage.width and g.coverage.height:
                canvas.paste(255 if spec.inverse else 0, (x+g.left, baseline-g.top), ink)
                max_width = max(max_width, x+g.left+g.coverage.width)
            x += g.advance
        return x

# Opening/closing punctuation stays with adjacent Chinese where possible.
OPEN = set('（《【「『“‘(')
CLOSE = set('，。！？；：、）》】」』”’!?;:,.%)')

def wrap(source, text, width, spec):
    if '\r' in text:
        text = text.replace('\r','')
    result, line = [], ''
    for c in text:
        if c == '\n':
            result.append(line); line=''; continue
        trial = line+c
        if line and source.width(trial,spec) > width:
            carry = ''
            if c in CLOSE and len(line) > 1:
                carry = line[-1]; line=line[:-1]
            elif line[-1] in OPEN:
                carry = line[-1]; line=line[:-1]
            if line: result.append(line)
            line=carry+c
            if source.width(line,spec) > width:
                raise ValueError('Line width too small for punctuation pair')
        else:
            line=trial
    if line: result.append(line)
    if any(source.width(s,spec)>width for s in result):
        raise ValueError('Unrenderable line')
    return result

def quantise(image, bpp):
    if bpp == 8: return image
    if bpp not in (2,4): raise ValueError('Coverage depth must be 2, 4, or 8')
    levels=(1<<bpp)-1
    return image.point(lambda v: ((v*levels+127)//255)*255//levels)

def toolchain():
    return {'stem_darkening_disabled':DARKENING_PROPERTIES,'freetype': '.'.join(map(str,ft.version())),
            'freetype_py': importlib.metadata.version('freetype-py'),
            'fonttools': importlib.metadata.version('fonttools'),
            'pillow': importlib.metadata.version('Pillow'),
            'FREETYPE_PROPERTIES': os.environ.get('FREETYPE_PROPERTIES',''),
            'raster_module_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'load_flags': {k: v[0]|ft.FT_LOAD_NO_BITMAP for k,v in ALGORITHMS.items()},
            'render_modes': {k: v[1] for k,v in ALGORITHMS.items()}}
