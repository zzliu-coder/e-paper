import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"host"))
from book_upload import upload

class TransferTests(unittest.TestCase):
    def exercise(self, corrupt=False):
        data=b"epub transfer fixture"*101
        stored=bytearray()
        def request(cmd,**args):
            if cmd=="book.begin":
                self.assertEqual(args["size"],len(data))
                return {"token":"test","chunk_max":1024}
            if cmd=="book.chunk":
                self.assertEqual(args["offset"],len(stored))
                stored.extend(bytes.fromhex(args["hex"]))
                return {"received":len(stored)}
            if cmd=="book.commit":
                return {"bytes":len(stored),"sha256":hashlib.sha256(stored).hexdigest(),"path":"/sdcard/books/test.epub"}
            if cmd=="book.read":
                offset=args["offset"]
                chunk=bytes(stored[offset:offset+1024])
                if corrupt: chunk=bytes([chunk[0]^1])+chunk[1:]
                return {"hex":chunk.hex(),"offset":offset,"bytes":len(chunk),"total":len(stored),"eof":offset+len(chunk)==len(stored)}
            raise AssertionError(cmd)
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"test.epub";p.write_bytes(data)
            if corrupt:
                with self.assertRaisesRegex(ValueError,"hash mismatch"):upload(p,"test.epub",request)
            else:self.assertEqual(upload(p,"test.epub",request)["transfer"],"PASS")
    def test_roundtrip(self):self.exercise()
    def test_corruption(self):self.exercise(True)
    def test_path_rejected(self):
        with self.assertRaises(ValueError):upload(Path("not-read"),"../bad.epub",None)

if __name__=="__main__":unittest.main()
