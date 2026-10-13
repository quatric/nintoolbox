#!/usr/bin/env python3
"""Synthetic Cursed Mountain bundle t.dat: one stored member "a.txt" and one zlib member "t.timg" (4x4 RGB5A3, all
white) in directory "wii\\test\\". usage: mk_ktm.py OUTDIR"""
import os, struct, sys, zlib

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
timg = (b'\xae\xfe\x00\x03\x01\x01\x00\x00' + struct.pack('>HHHH', 4, 0, 4, 0) + struct.pack('>HBB', 1, 6, 0)).ljust(0x20, b'\0')
timg += b'\xff\xff' * 16
members = [(b'a.txt', b'hello ktm', False), (b't.timg', timg, True)]
dirs = b'wii\\test\\\0'
es = 0x30
data = b''
ents = b''
for name, raw, z in members:
    stored = zlib.compress(raw) if z else raw
    ents += struct.pack('>6I', 0x1234, 0, len(data), len(stored), len(raw), int(z)) + name.ljust(es - 24, b'\0')
    data += stored.ljust((len(stored) + 15) & ~15, b'\0')
tsz = (len(ents) + 31) & ~31
ents = ents.ljust(tsz, b'\0')
hdr = b'\xd2\xf1\x7e\x00' + b't.dat'.ljust(0x1c, b'\0') + struct.pack('>8I', 0, len(members), 1, tsz, es, len(dirs), 0, 0)
open(os.path.join(d, 't.dat'), 'wb').write(hdr + dirs + ents + data)
