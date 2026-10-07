#!/usr/bin/env python3
"""Synthetic THQ Australia "pack" archive (see project/src/lib-thqpack.h):
data/a.rad + data/sub/b.txt. usage: mk_thqpack.py OUTDIR -> OUTDIR/TEST_DATA.PAK"""
import os, struct, sys

files = [('data/a.rad', b'rad payload'), ('data/sub/b.txt', b'hello pack\n')]
names = b''.join(n.encode() + b'\0' for n, _ in files)
noff = 0x18 + 16 * len(files)
ents, no, off = b'', 0, 0x800
for n, d in files:
    ents += struct.pack('>4I', no, off, len(d), 0)
    no += len(n) + 1
    off += (len(d) + 0x7ff) & ~0x7ff
total = off
out = struct.pack('>4s5I', b'pack', 1, len(names), total, noff, len(files)) + ents + names
out = out.ljust(0x800, b'\0')
for _, d in files:
    out += d.ljust((len(d) + 0x7ff) & ~0x7ff, b'\0')
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST_DATA.PAK'), 'wb').write(out)
