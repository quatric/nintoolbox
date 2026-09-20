#!/usr/bin/env python3
"""Synthetic Cat Daddy Games CDGaCube archive (see project/src/lib-catcar.h):
Data/a.txt (stored) + Data/sub/b.txt (zlib). usage: mk_catcar.py OUTDIR -> OUTDIR/test.CAR"""
import os, struct, sys, zlib

a = b'hello catcar\n'
b_plain = b'zlib member ' * 40
b = zlib.compress(b_plain)
names = ['Data/.', 'Data/..', 'Data/sub', 'Data/a.txt', 'Data/sub/b.txt']
# (flags, size, sector)
ents = [(0x10, 0, 0), (0x10, 0, 0), (0x30, 0, 0), (0x20, len(a), 0), (0x21, len(b), 0)]
hdr = 0x14 + 24 * len(ents) + sum(len(n) + 1 for n in names)
base = (hdr + 2047) // 2048
sectors = [0, 0, 0, base, base + 1]
out = b'CDGaCube' + struct.pack('<3I', base, len(ents), 24)
for i, (f, s, _) in enumerate(ents):
    out += struct.pack('<IQIII', f, 0x01c9c2ab00000000, s, i, sectors[i])
out += b''.join(n.encode() + b'\0' for n in names)
out = out.ljust(base * 2048, b'\0') + a.ljust(2048, b'\0') + b
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.CAR'), 'wb').write(out)
