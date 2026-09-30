#!/usr/bin/env python3
"""Synthetic Radical "ATG CORE CEMENT LIBRARY" (see project/src/lib-rcf.h):
four members incl. a backslash path and two names that differ only in case,
hash-sorted table, names in offset order, 0x800-aligned data.
usage: mk_rcf.py OUTDIR -> OUTDIR/T.rcf"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
files = [('script\\init.blua', b'\x1bLua' + b'x' * 100), ('levels\\a.p3d', b'P3D\xff' + bytes(range(200))),
         ('Readme.txt', b'first'), ('readme.TXT', b'second')]
hashes = [0x00050000, 0x10000000, 0x40000000, 0xff000001]      # any distinct values
AL = 0x800
n = len(files)
table_off = 0x3c
names = bytearray(struct.pack('<II', 0x800, 0))
for nm, data in files:
    nb = nm.encode() + b'\0'
    names += struct.pack('<IIII', 1184719449, 0x800, 0, len(nb)) + nb + b'\0\0\0'
names_off = (table_off + 12 * n + 0x100 + AL - 1) // AL * AL
data_off = names_off + (len(names) + AL - 1) // AL * AL
offs, cur = [], data_off
for nm, data in files:
    offs.append(cur); cur += (len(data) + AL - 1) // AL * AL
ents = sorted(zip(hashes, offs, [len(d) for _, d in files]))
hdr = b'ATG CORE CEMENT LIBRARY'.ljust(32, b'\0') + bytes([2, 1, 1, 1])
hdr += struct.pack('>6I', table_off, table_off + 12 * n, names_off, len(names), 0, n)
blob = hdr + b''.join(struct.pack('>3I', *e) for e in ents)
blob = blob.ljust(names_off, b'\0') + bytes(names)
blob = blob.ljust(data_off, b'\0')
for (nm, data), o in zip(files, offs):
    blob = blob.ljust(o, b'\0') + data
blob = blob.ljust(cur, b'\0')
open(os.path.join(out, 'T.rcf'), 'wb').write(blob)
