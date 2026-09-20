from __future__ import annotations
if __name__ == '__main__':
    from pathlib import Path as _P
    import runpy as _run, sys as _sys
    _entry=_P(__file__).resolve().parents[1]/'tools/fontbench/app'/'device_acceptance.py'
    _sys.path.insert(0,str(_entry.parent))
    _run.run_path(str(_entry),run_name='__main__')
    raise SystemExit

"""Compatibility entry point for Font Lab 4 device checks."""
from font_lab4_acceptance import main
if __name__ == '__main__': main()
