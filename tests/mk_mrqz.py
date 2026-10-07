#!/usr/bin/env python3
"""Synthetic "MRQZ" pak (see project/src/lib-mrqz.h): two members.
usage: mk_mrqz.py OUTDIR -> OUTDIR/TEST.pak"""
import os, struct, sys

hdr = 0x80
members = [('data/a.txt', b'alpha member\n', 0), ('data/dir/b.bin', b'\x01\x02\x03\x04', 0x200)]
n = len(members)
table_end = hdr + 8 + 0x4c * n
base = (table_end + hdr - 1) // hdr * hdr
size = base + 0x200 + 4
out = bytearray(size)
out[0:4] = b'MRQZ'
struct.pack_into('<IHHI', out, 4, size - 8, 2, hdr, n)
for i, (nm, d, off) in enumerate(members):
    p = hdr + 8 + 0x4c * i
    out[p:p + len(nm)] = nm.encode()
    struct.pack_into('<3I', out, p + 64, off, len(d), 0)
    out[base + off:base + off + len(d)] = d
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.pak'), 'wb').write(bytes(out))
