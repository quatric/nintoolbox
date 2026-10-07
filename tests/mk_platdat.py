#!/usr/bin/env python3
"""Synthetic PlatinumGames "DAT" archive (see project/src/lib-platdat.h): two members.
usage: mk_platdat.py OUTDIR -> OUTDIR/TEST.dat"""
import os, struct, sys

members = [('one.txt', b'first member\n'), ('two.bin', b'\x01\x02\x03\x04')]
n = len(members)
ot = 0x20
et = ot + 4 * n
nt = et + 4 * n
ln = 16
st = nt + 4 + ln * n
ht = st + 4 * n
data = (ht + 8 * n + 0xf) & ~0xf
out = bytearray(data + 0x40)
out[0:4] = b'DAT\0'
struct.pack_into('>6I', out, 4, n, ot, et, nt, st, ht)
struct.pack_into('>I', out, nt, ln)
off = data
for i, (nm, d) in enumerate(members):
    struct.pack_into('>I', out, ot + 4 * i, off)
    struct.pack_into('>I', out, st + 4 * i, len(d))
    out[nt + 4 + ln * i:nt + 4 + ln * i + len(nm)] = nm.encode()
    out[off:off + len(d)] = d
    off += 0x10
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.dat'), 'wb').write(bytes(out))
