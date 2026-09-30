#!/usr/bin/env python3
"""Synthetic Exient "XPK" archive (see project/src/lib-xpk.h): a directory
record, a root file, zlib and stored files inside the directory, and a
duplicate name that must be de-duplicated.
usage: mk_xpk.py OUTDIR -> OUTDIR/T.pak"""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
TS = 0x5217cb92

# (name, payload, compress) ; the dir 'sub' owns the entries after the root files
root = [('a.lua', b'print("hello")\n' * 40, True), ('raw.bin', bytes(range(200)), False)]
sub = [('b.lua', b'x = 1\n' * 100, True), ('b.lua', b'dup\n' * 30, True)]
names = b''
def add(n):
    global names
    o = len(names); names += n.encode() + b'\0'; return o

n_dir = 1
rows = [None] * (n_dir + len(root) + len(sub))
dir_first = n_dir + len(root)
rows[0] = ('sub', 0, dir_first, 0, 0, len(sub))
for i, (n, p, c) in enumerate(root):
    rows[n_dir + i] = (n, p, None, c)
for i, (n, p, c) in enumerate(sub):
    rows[dir_first + i] = (n, p, None, c)

nt_placeholder = None
E_ = len(rows)
# name offsets first
name_off = [add(r[0]) for r in rows]
data_start = 0x50 + E_ * 32 + len(names)
blob = b''; ent = []
for i, r in enumerate(rows):
    if i == 0:
        ent.append(struct.pack('>8I', 0, name_off[i], 0, dir_first, 0, 0, len(sub), 0)); continue
    n, p, _, c = r
    if c:
        z = zlib.compress(p, 9)
        ent.append(struct.pack('>8I', 0, name_off[i], len(p), data_start + len(blob), 1, TS, len(z), 0)); blob += z
    else:
        ent.append(struct.pack('>8I', 0, name_off[i], len(p), data_start + len(blob), 0, TS, 0, 0)); blob += p

files = E_ - 1
head = struct.pack('>4I', 0x58504b01, 2, files, len(names)).ljust(0x50, b'\0')
open(os.path.join(out, 'T.pak'), 'wb').write(head + b''.join(ent) + names + blob)
