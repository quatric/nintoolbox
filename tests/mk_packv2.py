#!/usr/bin/env python3
"""Synthetic "PACK" v2 archive (see project/src/lib-packv2.h): one stored and
one packed member. usage: mk_packv2.py OUTDIR -> OUTDIR/TEST.pak"""
import os, struct, sys

stored = b'stored member data\n'
packed = b'\x74\x01packed-bytes\x00'
members = [('global/a.txt', stored, len(stored), 0x30), ('menu/b.pic', packed, 99, 0x77)]
count = len(members)
tab_end = 0x10 + 0x18 * count
names = b''
name_offs = []
for n, _, _, _ in members:
    name_offs.append(tab_end + len(names))
    names += n.encode() + b'\0'
hdr_end = (tab_end + len(names) + 0x7ff) & ~0x7ff
out = bytearray(hdr_end)
out[0:4] = b'PACK'
struct.pack_into('<3I', out, 4, 2, count, hdr_end)
off = hdr_end
for i, (n, d, s2, fl) in enumerate(members):
    struct.pack_into('<6I', out, 0x10 + 0x18 * i, len(d), s2, off, name_offs[i], 0x1234 + i, fl)
    off += (len(d) + 0x7ff) & ~0x7ff
out[tab_end:tab_end + len(names)] = names
for _, d, _, _ in members:
    out += d.ljust((len(d) + 0x7ff) & ~0x7ff, b'\0')
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.pak'), 'wb').write(bytes(out))
