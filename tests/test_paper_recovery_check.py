import hashlib,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from paper_recovery_check import check_app,inspect_backup

def image():
    h=bytearray(24);h[0]=0xe9;h[1]=1;h[12]=9;h[23]=1
    h+=struct.pack('<II',0x3c000000,4)+b'abcd';crc=0xef
    for b in b'abcd':crc^=b
    h+=b'\0'*(15-len(h)%16)+bytes([crc]);h+=hashlib.sha256(h).digest();return h

class RecoveryTest(unittest.TestCase):
    def test_image_integrity(self):
        data=image();self.assertEqual(check_app(data,0,len(data))['checksum'],'PASS')
        for offset in (0,12,28,35,len(data)-1):
            bad=bytearray(data);bad[offset]^=1
            with self.assertRaises(ValueError):check_app(bad,0,len(bad))
    def test_backup_and_partition_validation(self):
        data=bytearray(b'\xff'*0x1000000);entry=struct.pack('<HBBII16sI',0x50aa,0,16,0x10000,0x10000,b'ota_0',0)
        data[0x8000:0x8020]=entry;data[0x8020:0x8040]=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(entry).digest()
        app=image();data[0x10000:0x10000+len(app)]=app
        with tempfile.TemporaryDirectory() as root:
            p=Path(root)/'backup.bin';p.write_bytes(data);digest=hashlib.sha256(data).hexdigest()
            self.assertEqual(inspect_backup(p,digest)['result'],'PASS')
            with self.assertRaises(ValueError):inspect_backup(p,'0'*64)
            data[0x8030]^=1;p.write_bytes(data)
            with self.assertRaises(ValueError):inspect_backup(p,hashlib.sha256(data).hexdigest())
if __name__=='__main__':unittest.main()
