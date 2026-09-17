"""Test the same C++ UI model as the device and export actual drawing commands."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='metalio-ui-') as directory:
    exe = Path(directory) / 'ui-test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(root/'main'), str(root/'main/ui_demo.cc'),
                    str(root/'tests/ui_demo_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        with a.output.open('w') as out:
            subprocess.run([str(exe), '--dump'], stdout=out, check=True)
