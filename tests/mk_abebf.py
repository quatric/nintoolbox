#!/usr/bin/env python3
"""Synthetic Ubisoft Magma ABE BigFile (see project/src/lib-abebf.h) with two
table chunks: a stored member, a two-block LZO member (the second block ends
without the end-of-stream marker, like retail) and a skipped shadow reference.
usage: mk_abebf.py OUTDIR -> OUTDIR/TEST.BF"""
import os, struct, sys

REC = 200


def lzo_literals(b):
    assert 4 <= len(b) <= 18
    return bytes([len(b) - 3]) + b


def record(name, key, off, size):
    r = bytearray(REC)
    r[:len(name)] = name
    struct.pack_into('<I', r, 0x58, size + 0x20)
    struct.pack_into('<I', r, 0x64, key)
    struct.pack_into('<I', r, 0x6c, off)
    struct.pack_into('<I', r, 0x74, size)
    return bytes(r)


def payload(kind, stored, unpacked, body):
    return struct.pack('<4I', stored, unpacked, 0, kind) + b'\xaa' * 16 + body


plain = b'hello abe pack\n'                       # type 2
blk0, blk1 = b'first block data', b'second block 12'  # 16 and 14 bytes
c0, c1 = lzo_literals(blk0) + b'\x11\0\0', lzo_literals(blk1)  # block 0 with marker
lzo_body = struct.pack('<III', 2, len(c0), len(c1)) + c0 + c1
shadow = b'<$shadow$>/aa/bb.bin'

TABLE = 0x14d8
CH0 = 3 * REC + 12
CH1_OFF = TABLE + 12 + 3 * REC
data0 = TABLE + 12 + 3 * REC + 12 + 2 * REC
chunks = []
blobs = []
off = data0
for kind, name, key, stored, unp, body in [
    (2, b'stored.txt', 0x11, len(plain), len(plain), plain),
    (4, b'lzo\0bin', 0x22, len(lzo_body), len(blk0) + len(blk1), lzo_body),
    (3, b'shadowed.bik', 0x33, len(shadow), 0x1000, shadow),
    (2, b'second chunk.txt', 0x44, 3, 3, b'abc'),
]:
    blobs.append((name, key, off, kind, stored, unp, body))
    off += 0x20 + len(body)
recs = [record(n, k, o, u) for n, k, o, kind, s, u, b in blobs]
out = bytearray(TABLE)
out[0:4] = b'ABE\0'
struct.pack_into('<III', out, 4, 4, len(blobs), 1)
struct.pack_into('<I', out, 0x18, TABLE)
chunk0 = struct.pack('<III', 3, 0, CH1_OFF) + b''.join(recs[:3])
chunk1 = struct.pack('<III', 2, 0xffffffff, 0xaaaaaaaa) + recs[3] + bytes(REC)
out += chunk0 + chunk1
assert len(out) == data0, (len(out), data0)
for name, key, o, kind, stored, unp, body in blobs:
    out += payload(kind, stored, unp, body)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.BF'), 'wb').write(bytes(out))
