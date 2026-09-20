"""Reject stale UI subsets before provisioning. Does not access the device."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def required_chars(root):
    required = set(range(32, 127))
    for folder in (root / 'components/paper_core', root / 'main/paper_shell'):
        for file in folder.rglob('*'):
            if file.suffix in ('.cpp', '.cc', '.h', '.hpp'):
                required.update(ord(c) for c in file.read_text() if ord(c) > 127)
    return required - {0xfeff, 0x200b}


def check(assets, root):
    folder = assets / 'paper/fonts'
    manifest = json.loads((folder / 'manifest-ui.json').read_text())
    assert manifest.get('test_only') is False
    needed = required_chars(root)
    expected = {f'misans-ui-{weight}-{px}.pgf' for weight in (400, 500, 700) for px in range(16, 41)}
    entries = {entry['path']: entry for entry in manifest['files']}
    assert set(entries) == expected, 'Incomplete UI profiles'
    for name, entry in entries.items():
        data = (folder / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == entry['sha256'], name
        count, offset = struct.unpack_from('<II', data, 12)
        assert offset == 80 and len(data) >= offset + count * 24, name
        glyphs = {struct.unpack_from('<I', data, offset + i * 24)[0] for i in range(count)}
        missing = needed - glyphs
        assert not missing, f'{name}: missing {"".join(map(chr, sorted(missing)))}'
    return dict(profiles=len(entries), required_glyphs=len(needed), result='PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.assets, Path(__file__).resolve().parents[1])))
