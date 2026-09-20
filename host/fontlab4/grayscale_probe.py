"""Build true-level calibration input. This module never sends a waveform.
The fixed firmware's I1 path cannot retain the two intermediate levels.
A matched, verified panel driver must consume this data in a separate experiment.
"""
from pathlib import Path
import argparse,json,hashlib

def make_probe(width=480,height=800):
    if width%4 or height<4: raise ValueError('Unsupported dimensions')
    # top to bottom: 0 black, 1 dark, 2 light, 3 white. Packed 2-bit MSB first.
    return b''.join(bytes([level*85])*(width//4) for y in range(height) for level in [min(3,y*4//height)])

def require_gray_backend(capability):
    if capability.get('output_bpp')!=2 or not capability.get('panel_id_verified') or not capability.get('waveform_id'):
        raise RuntimeError('True gray backend unavailable; do not send this through I1 or use guessed LUTs')

def main(argv=None):
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args(argv)
    a.out.mkdir(parents=True,exist_ok=True)
    data=make_probe();path=a.out/'four-levels-480x800.2bit'
    with path.open('xb') as f:f.write(data)
    info={'width':480,'height':800,'output_bpp':2,'encoding':'MSB first, 0 black, 1 dark, 2 light, 3 white',
          'sha256':hashlib.sha256(data).hexdigest(),'expected_bytes':96000,
          'status':'INPUT_READY; PANEL_DRIVER_NOT_VERIFIED',
          'tests':['repeat after white','repeat after black','cold restart','gray to mono restoration'],
          'panel_id_verified':False,'waveform_id':None}
    with (a.out/'probe.json').open('x') as f:json.dump(info,f,indent=2)
if __name__=='__main__':main()
