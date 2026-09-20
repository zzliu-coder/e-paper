"""Offline ESP32-S3 full-backup integrity check. Never opens a device or writes flash.

An expected SHA must come from an independently retained receipt. This checks
bytes/partition bounds/app checksums, not hardware identity, current eFuses or
whether a physical recovery will succeed.
"""
import argparse,hashlib,json,struct
from pathlib import Path

def check_app(data,offset,size):
    end=offset+size;header=data[offset:offset+24]
    if len(header)!=24 or header[0]!=0xe9 or not 1<=header[1]<=16:raise ValueError('Invalid app image header')
    if struct.unpack_from('<H',header,12)[0]!=9:raise ValueError('Application is not for ESP32-S3')
    pos=offset+24;checksum=0xef
    for _ in range(header[1]):
        if pos+8>end:raise ValueError('Truncated segment header')
        _,length=struct.unpack_from('<II',data,pos);pos+=8
        if pos+length>end:raise ValueError('Segment outside application partition')
        for byte in data[pos:pos+length]:checksum^=byte
        pos+=length
    checksum_at=offset+((pos-offset)//16+1)*16-1
    if checksum_at>=end or data[checksum_at]!=checksum:raise ValueError('Application checksum mismatch')
    pos=checksum_at+1
    if header[23] not in (0,1):raise ValueError('Invalid image hash flag')
    if header[23]:
        if pos+32>end or hashlib.sha256(data[offset:pos]).digest()!=data[pos:pos+32]:raise ValueError('Application validation hash mismatch')
        pos+=32
    return {'offset':hex(offset),'image_bytes':pos-offset,'checksum':'PASS','validation_hash':'PASS' if header[23] else 'ABSENT'}

def inspect_backup(path,expected_sha):
    if len(expected_sha)!=64 or any(c not in '0123456789abcdef' for c in expected_sha):raise ValueError('Expected lowercase SHA256 required')
    if path.stat().st_size!=0x1000000:raise ValueError('Expected complete 16 MiB backup')
    data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if digest!=expected_sha:raise ValueError('Backup differs from retained SHA256')
    parts=[];table=bytearray();has_md5=False
    for pos in range(0x8000,0x9000,32):
        entry=data[pos:pos+32];magic=entry[:2]
        if magic==b'\xff\xff':break
        if magic==b'\xeb\xeb':
            if has_md5 or entry[16:]!=hashlib.md5(table).digest():raise ValueError('Partition MD5 mismatch')
            has_md5=True;break
        if magic!=b'\xaa\x50':raise ValueError('Invalid partition entry')
        _,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',entry)
        if not size or offset<0x9000 or offset+size>len(data):raise ValueError('Partition outside data area')
        if offset%4096 or size%4096:raise ValueError('Unaligned partition')
        if kind==0 and offset%0x10000:raise ValueError('Unaligned app partition')
        for p in parts:
            if offset<p['address']+p['bytes'] and offset+size>p['address']:raise ValueError('Overlapping partitions')
        parts.append({'name':label.split(b'\0')[0].decode('ascii'),'type':kind,'subtype':sub,'address':offset,'bytes':size,'flags':flags})
        table.extend(entry)
    if not parts or not has_md5:raise ValueError('Partition table/MD5 missing')
    apps=[]
    for p in parts:
        if p['type']==0:
            offset,size=p['address'],p['bytes']
            if data[offset:offset+size]==b'\xff'*size:apps.append({'name':p['name'],'state':'EMPTY'})
            else:apps.append(dict(name=p['name'],state='VALID',**check_app(data,offset,size)))
    if not any(a['state']=='VALID' for a in apps):raise ValueError('No valid application image')
    return {'result':'PASS','scope':'offline backup bytes only','path':str(path.resolve()),'bytes':len(data),'sha256':digest,
            'partition_md5':'PASS','partitions':parts,'apps':apps,'physical_restore':'NOT_PROVEN','current_security':'NOT_CHECKED'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('backup',type=Path);p.add_argument('--sha256',required=True);p.add_argument('--out',type=Path)
    a=p.parse_args();result=inspect_backup(a.backup,a.sha256);payload=json.dumps(result,ensure_ascii=False,indent=2)
    if a.out:
        a.out.parent.mkdir(parents=True,exist_ok=True)
        with a.out.open('x') as f:f.write(payload+'\n')
    print(payload)
if __name__=='__main__':main()
