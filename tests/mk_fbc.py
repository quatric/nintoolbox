#!/usr/bin/env python3
"""Synthetic h.a.n.d. FBC bundle (see project/src/lib-fbc.h) with two members.
usage: mk_fbc.py OUTDIR -> OUTDIR/T.fbc (members a.bin = 100 x 0x41, B.TXT = 2000 x 0x42)"""
import os, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
members = [('a.bin', b'A' * 100), ('B.TXT', b'B' * 2000)]
n = len(members)
tab = lambda vals: struct.pack('>%dI' % len(vals), *vals).ljust(0x20, b'0')
hdr = struct.pack('>HHI6I', 0x14, 6, n, 0x40, 0x60, 0x80, 0x60, 0, 0).ljust(0x40, b'\0')
data, offs = b'', []
for _, b in members:
    offs.append(0x60 + len(data)); data += b + bytes(-len(b) & 31)
body = hdr + tab(offs) + tab([0] * n) + tab([0] * n) + data
slot = lambda b: b.ljust(32, b'0')
for _, b in members: body += slot(struct.pack('>I', len(b)))
for nm, _ in members: body += slot(nm.encode() + b'\0')
open(out + '/T.fbc', 'wb').write(body)
