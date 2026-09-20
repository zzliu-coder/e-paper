#!/usr/bin/env python3
"""Verify a FontBench SD bundle before copying it to a card."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from fontbench.bank import decode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    root = args.bundle.resolve()
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    hashes = json.loads((root / "tiles-sha256.json").read_text(encoding="utf-8"))
    catalog = manifest["catalog"]
    assert len(catalog) == 8
    assert all(c["available"] and c["weights"] for c in catalog)
    assert not manifest["invalid_tiles"]
    assert manifest["tile_count"] == len(hashes)
    for name, expected in hashes.items():
        tile = root / "tiles" / name
        data = tile.read_bytes()
        assert hashlib.sha256(data).hexdigest() == expected, name
        decode(data, manifest["bundle_tag"])
    actual = sum(1 for _ in (root / "tiles").glob("*.t5"))
    assert actual == manifest["tile_count"]
    print(json.dumps({
        "bundle": str(root),
        "bundle_tag": manifest["bundle_tag"],
        "tile_count": actual,
        "families": [{"id": c["id"], "weights": c["weights"]} for c in catalog],
        "invalid_tiles": len(manifest["invalid_tiles"]),
        "status": "PASS",
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
