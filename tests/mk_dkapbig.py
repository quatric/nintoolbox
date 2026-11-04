#!/usr/bin/env python3
# Synthetic BIGFILE0.TOC/.DAT (Totally Spies! Totally Party): a DKAP (LZO1X) text member named via the path
# table, plus a DKAP-packed 4x4 RGBA8 "_NGCBM01" bitmap.
import struct, sys, os
out = sys.argv[1]
def dkap(data):   # literal-only LZO1X stream + end-of-stream marker
    assert 4 <= len(data) <= 238
    return b'DKAP' + struct.pack('<I', len(data)) + bytes([17 + len(data)]) + data + b'\x11\x00\x00'
txt = dkap(b'hello dkap bigfile')
bm = b'_NGCBM01' + struct.pack('>HHHBB', 4, 4, 6, 1, 0) + struct.pack('>II', 0, 0x20) + b'\0' * 8
bm += bytes((i * 7) & 255 for i in range(64))
bmp = dkap(bm)
def rec(i, p):
    b = struct.pack('<IB', i, len(p)) + p + b'\0'
    return b + b'\0' * (-len(b) % 4)
tbl = struct.pack('<I', 2) + rec(1, b'D:\\TSPIES\\PROD\\GAME\\VDB_REPOSITORY\\FOO\\BAR.TXT') + rec(2, b'D:\\TSPIES\\PROD\\GAME\\VDB_REPOSITORY\\FOO\\PIC.TGA')
members = [(1, 0xb067ebb2, txt), (2, 0xb067ebb2, bmp), (3, 0xf5fadcd5, tbl)]
dat = b''; offs = []
for _, _, d in members:
    offs.append(len(dat)); dat += d
head = struct.pack('<IIII', len(members), 1, members[0][1], len(members[0][2])).ljust(0x24, b'\0')
toc = head
for (i, h, d), o in zip(members[1:], offs[1:]):
    toc += struct.pack('<9I', 0, i, h, len(d), o, o, o, o, 0)
open(os.path.join(out, 'BIGFILE0.TOC'), 'wb').write(toc)
open(os.path.join(out, 'BIGFILE0.DAT'), 'wb').write(dat)
