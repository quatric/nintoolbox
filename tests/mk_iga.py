#!/usr/bin/env python3
"""Synthetic Skylanders "IGA" archive (see project/src/lib-iga.h): two stored
members. usage: mk_iga.py OUTDIR -> OUTDIR/TEST.arc"""
import os, struct, sys

members = [('c:/tfb/build/wii/levels/a.txt', b'first member\n'), ('c:/tfb/build/wii/levels/dir/b.bin', b'\x01\x02\x03\x04')]
n = len(members)
table_end = 0x30 + 16 * n
pool = b''
offs = []
for name, _ in members:
    offs.append(4 * n + len(pool))
    pool += name.encode() + b'\0'
names = b''.join(struct.pack('<I', o) for o in offs) + pool
data_off = 0x800
ents = []
for _, d in members:
    ents.append((data_off, len(d)))
    data_off += (len(d) + 0x7ff) & ~0x7ff
names_off = data_off
out = bytearray(names_off + len(names))
out[0:4] = b'IGA\x1a'
struct.pack_into('<7I', out, 4, 4, table_end - 0x30, n, 0, 0, names_off, len(names))
for i in range(n):
    struct.pack_into('<I', out, 0x30 + 4 * i, 0x1000 + i)
    struct.pack_into('<3I', out, 0x30 + 4 * n + 12 * i, ents[i][0], ents[i][1], 0xffffffff)
for (o, _), (_, d) in zip(ents, members):
    out[o:o + len(d)] = d
out[names_off:] = names
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.arc'), 'wb').write(bytes(out))

# Madagascar 2 style version-2 archive with a ".bld" extension.
v2 = bytearray(out)
struct.pack_into('<I', v2, 4, 2)
open(os.path.join(sys.argv[1], 'V2.bld'), 'wb').write(bytes(v2))
