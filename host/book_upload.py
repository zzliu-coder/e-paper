"""Pack an Apple Books EPUB directory or upload a ZIP EPUB; verify every SD byte.

Original book is read-only. New device names only; no automatic retry or overwrite.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import zipfile
from service import call, DEFAULT_SOCKET

LIMIT = 16 * 1024 * 1024


def inspect_epub(path):
    with zipfile.ZipFile(path) as z:
        if len(z.infolist()) > 4096:
            raise ValueError("Too many ZIP entries")
        if z.read("mimetype") != b"application/epub+zip":
            raise ValueError("Not an EPUB")
        if "META-INF/encryption.xml" in z.namelist():
            raise ValueError("Encrypted/obfuscated resources are not supported")
        if any(i.flag_bits & 1 or i.file_size > 16 * 1024 * 1024 for i in z.infolist()):
            raise ValueError("Encrypted or oversized ZIP entry")
        if sum(i.file_size for i in z.infolist()) > 64 * 1024 * 1024:
            raise ValueError("Unpacked book too large")
        if z.testzip() is not None:
            raise ValueError("EPUB ZIP CRC failed")
        z.read("META-INF/container.xml")


def prepare(source, output):
    if not source.is_dir():
        inspect_epub(source)
        return source
    # Apple Books stores EPUBs as directories. Repackage all book resources,
    # never convert the text, modify the original, or include Apple metadata.
    if (source / "mimetype").read_bytes() != b"application/epub+zip":
        raise ValueError("Not an unpacked EPUB")
    files = sorted(p for p in source.rglob("*") if p.is_file() and not any(x.startswith(".") for x in p.relative_to(source).parts))
    if any(p.is_symlink() or source.resolve() not in p.resolve().parents for p in files):
        raise ValueError("Symlink book resources are not accepted")
    if sum(p.stat().st_size for p in files) > 64 * 1024 * 1024:
        raise ValueError("Unpacked book too large")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
        for path in files:
            relative = path.relative_to(source).as_posix()
            if relative == "mimetype" or relative in {"iTunesMetadata.plist", "iTunesArtwork"}:
                continue
            z.write(path, relative)
    inspect_epub(output)
    return output


def upload(path, name, request):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,74}\.epub", name) or ".." in name:
        raise ValueError("Use a safe ASCII .epub device filename")
    size = path.stat().st_size
    if not 0 < size <= LIMIT:
        raise ValueError("EPUB must be at most 16 MiB")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    begin = request("book.begin", name=name, size=size, sha256=digest)
    token = begin["token"]
    offset = 0
    try:
        with path.open("rb") as source:
            while True:
                data = source.read(min(1024, begin["chunk_max"]))
                if not data:
                    break
                reply = request("book.chunk", token=token, offset=offset, hex=data.hex())
                offset += len(data)
                if reply["received"] != offset:
                    raise ValueError("Device offset mismatch")
                if offset % (128 * 1024) == 0:
                    print(f"Uploaded {offset}/{size}", flush=True)
        receipt = request("book.commit", token=token)
        if receipt["sha256"] != digest or receipt["bytes"] != size:
            raise ValueError("Commit manifest mismatch")
    except Exception:
        # Do not retry writes. Abort is limited to this token's unfinished file.
        try:
            request("book.abort", token=token)
        except Exception:
            pass
        raise
    readback = hashlib.sha256()
    offset = 0
    while offset < size:
        reply = request("book.read", name=name, offset=offset)
        data = bytes.fromhex(reply["hex"])
        if reply["offset"] != offset or reply["bytes"] != len(data) or not data or reply["total"] != size:
            raise ValueError("Readback size/offset mismatch")
        offset += len(data)
        if offset > size or bool(reply["eof"]) != (offset == size):
            raise ValueError("Unexpected readback EOF")
        readback.update(data)
    if readback.hexdigest() != digest:
        raise ValueError("SD readback hash mismatch")
    return {"file": str(path), "device_path": receipt["path"], "bytes": size,
            "sha256": digest, "readback_sha256": readback.hexdigest(), "transfer": "PASS",
            "reading_acceptance": "SEPARATE_CHECK_REQUIRED"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    path = prepare(args.source, args.output)
    if args.prepare_only:
        print(path)
        return
    identity = None
    def request(command, **kwargs):
        nonlocal identity
        result = call(args.socket, {"cmd": command, "args": kwargs})
        if not result.get("ok"):
            raise RuntimeError(result)
        p = result["result"]
        current = (p["device_id"], p["boot_id"])
        if identity is None:
            identity = current
        elif identity != current:
            raise RuntimeError("Device rebooted/changed during transfer; stopped")
        return p
    receipt = upload(path, args.name, request)
    receipt["device_id"], receipt["boot_id"] = identity
    args.output.with_suffix(".receipt.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False)+"\n")
    print(json.dumps(receipt, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
