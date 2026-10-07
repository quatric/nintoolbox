#!/usr/bin/env python3
"""Synthetic Winning Post "FCAT" container (see project/src/lib-fcat.h): two raw members.
usage: mk_fcat.py OUTDIR -> OUTDIR/TEST.dat"""
import os, struct, sys

members = [b'first member data\n', b'\x01\x02\x03\x04\x05']
n = len(members)
out = bytearray(b'FCAT' + struct.pack('>I', n))
off = 0x40
tab = b''
for m in members:
    tab += struct.pack('>2I', off, len(m))
    off += (len(m) + 0x3f) & ~0x3f
out += tab
out = out.ljust(0x40, b'\0')
for m in members:
    out += m.ljust((len(m) + 0x3f) & ~0x3f, b'\0')
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.dat'), 'wb').write(bytes(out))
