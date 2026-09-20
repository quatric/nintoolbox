#!/usr/bin/env python3
"""Synthetic Terminal Reality POD5 archive (see project/src/lib-termpod.h):
sound\\a.wav + data\\b.txt. usage: mk_termpod.py OUTDIR -> OUTDIR/TEST.POD"""
import os, struct, sys

files = [('sound\\a.wav', b'RIFFxxxxWAVE'), ('data\\b.txt', b'hello pod\n')]
names = b''.join(n.encode() + b'\0' for n, _ in files)
eoff = 0x120 + 80 + 4
ent_size = 28 * len(files)
data_off = eoff + ent_size + len(names)
hdr = bytearray(eoff)
hdr[0:4] = b'POD5'
struct.pack_into('<I', hdr, 0x58, len(files))
struct.pack_into('<I', hdr, 0x108, eoff)
struct.pack_into('<I', hdr, 0x110, len(names))
ents, no, off = b'', 0, data_off
for n, d in files:
    ents += struct.pack('<7I', no, len(d), off, len(d), 0, 0, 0)
    no += len(n) + 1
    off += len(d)
out = bytes(hdr) + ents + names + b''.join(d for _, d in files)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.POD'), 'wb').write(out)
