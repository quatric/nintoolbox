#!/usr/bin/env python3
"""Synthetic CRI AAX (@UTF table "AAX", columns data / lpflg) with two ADX segments. usage: mk_aax.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def adx(fill):
    h = bytearray(0x24); h[0] = 0x80; struct.pack_into('>H', h, 2, 0x20); h[4:8] = bytes([3, 18, 4, 1])
    struct.pack_into('>II', h, 8, 32000, 32); h[0x24-4:0x24] = b'\0\0\0\0'
    return bytes(h[:0x20]) + b'(c)CRI' + bytes([fill]) * 18
segs = [adx(0x11), adx(0x22)]
strings = b'<NULL>\0AAX\0data\0lpflg\0'
data = b''.join(segs)
fields = bytes([0x5b]) + struct.pack('>I', 11) + bytes([0x50]) + struct.pack('>I', 16)
rows, off = b'', 0
for s in segs:
    rows += struct.pack('>IIB', off, len(s), 0); off += len(s)
ro = 0x18 + len(fields); so = ro + len(rows); do = so + len(strings)
body = struct.pack('>HHIIIHHI', 0, ro, so, do, 7, 2, 9, 2) + fields + rows + strings + data
open(os.path.join(d, 't.aax'), 'wb').write(b'@UTF' + struct.pack('>I', len(body)) + body)
