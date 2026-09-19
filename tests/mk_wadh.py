#!/usr/bin/env python3
"""Synthetic Data Design Interactive WADH archive (see project/src/lib-wadh.h):
DATA/Interface/a.txt + DATA/b.bin. usage: mk_wadh.py OUTDIR -> OUTDIR/DataWII.wad"""
import os, struct, sys

NONE = 0xFFFFFFFF
files = {2: ('DATA/Interface/a.txt', b'hello wadh\n'), 3: ('DATA/b.bin', bytes(range(40)))}
# entry order: 0 root, 1 DATA, 2 Interface, 3 a.txt, 4 b.bin
names = ['DATA', 'Interface', 'a.txt', 'b.bin']
name_off, blob = {}, b''
for n in names:
    name_off[n] = len(blob); blob += n.encode() + b'\0'
a, b = files[2][1], files[3][1]
ents = [
    (NONE, NONE, 0, 0, 0, 0, 1, NONE),                                   # root -> last child DATA
    (name_off['DATA'], 0, 0, 0, 0, 1, 4, NONE),                          # DATA: last child b.bin
    (name_off['Interface'], 0, 0, 0, 0, 1, 3, NONE),                     # Interface: last child a.txt
    (name_off['a.txt'], 0, 0, len(a), len(a), 1, NONE, NONE),
    (name_off['b.bin'], 0, 0x20, len(b), len(b), 1, NONE, 2),            # prev sibling: Interface
]
hdr_size = 0x10 + 32 * len(ents) + len(blob)
out = b'WADH' + struct.pack('<3I', hdr_size, len(ents), len(blob))
for e in ents:
    out += struct.pack('<8I', *e)
out += blob + a + b'\0' * (0x20 - len(a)) + b
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'DataWII.wad'), 'wb').write(out)
