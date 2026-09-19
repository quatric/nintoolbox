#!/usr/bin/env python3
"""Synthetic Grip Entertainment .res (see project/src/lib-gripres.h): a strg,
an indx and one 8x8 solid-red CMPR surf. usage: mk_gripres.py OUTDIR ->
OUTDIR/test.res"""
import os, struct, sys

B = lambda *v: struct.pack('>%dI' % len(v), *v)
def pad(b, a): return b + b'\0' * (-len(b) % a)

strg = pad(b'test/red\0', 0x20)
# indx: count, 4, one entry {name rel ptr, 'surf', offset}; the name lives in strg
surf_hdr = bytearray(0x40)
surf_hdr[0x0f] = 2
surf_hdr[0x10:0x14] = struct.pack('>HH', 8, 8)
surf = bytes(surf_hdr) + struct.pack('>HHI', 0xF800, 0, 0) * 4
base = 0x60
indx_off = len(strg)
indx = bytearray(B(1, 4) + b'\0' * 12)
indx[8:12] = struct.pack('>i', 0 - (base + indx_off + 8))   # name at strg start
indx[12:16] = b'surf'
indx = pad(bytes(indx), 0x20)
surf_off = indx_off + len(indx)
data = strg + indx + surf
secs = [(b'strg', 0, len(strg)), (b'indx', indx_off, len(indx)), (b'surf', surf_off, len(surf))]
table = B(len(secs), 4) + b''.join(t + B(o, s, 0x20, 0, 1) for t, o, s in secs)
types = [(b'strg', 1), (b'indx', 4), (b'surf', 0xb5)]
hdr = bytearray(base)
hdr[0:4] = b'res\n'
hdr[4:8] = bytes([0x0c, 1, 5, 0])
struct.pack_into('>I', hdr, 0x0c, base)
struct.pack_into('>III', hdr, 0x2c, base + len(data), len(table), 0)
struct.pack_into('>I', hdr, 0x3c, len(types))
for i, (t, v) in enumerate(types):
    hdr[0x40 + 8 * i:0x48 + 8 * i] = t + struct.pack('>HH', v, 0)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.res'), 'wb').write(bytes(hdr) + data + table)
