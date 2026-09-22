#!/usr/bin/env python3
"""Synthetic Toshi TSFB keyframe library (see project/src/lib-toshi.h):
a "keylib" symbol with 2 packed-s16 translations, 3 packed-s16 quaternions
and 0 scales. usage: mk_tkl.py OUTDIR"""
import os, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)

name = b'Test\0'
hdr_size = 52
name_off = hdr_size
name_pad = (len(name) + 3) & ~3
to = name_off + name_pad
translations = [(100, -200, 300), (-4096, 4096, 0)]
tdata = b''.join(struct.pack('>3h', *t) for t in translations)
qo = to + len(tdata)
quaternions = [(0, 0, 0, 32767), (11585, 0, 0, 11585), (-11585, 11585, 0, 16384)]
qdata = b''.join(struct.pack('>4h', *q) for q in quaternions)
so = qo + len(qdata)

hdr = B(name_off) + struct.pack('>3f', 0.0001220703125, 0.0001220703125, 0.0001220703125)
hdr += struct.pack('>6i', len(translations), len(quaternions), 0, 6, 8, 4)
hdr += B(to, qo, so)
sect = hdr + name + bytes(name_pad - len(name)) + tdata + qdata

symb = B(1) + struct.pack('>HHII', 0, 0, 0x1234, 0) + b'keylib\0'
symb += bytes(-len(symb) & 3)

def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)

def tsfb(sec_hunk):
    body = b'FBRT' + hunk(b'XRDH', bytes(24)) + sec_hunk + hunk(b'BMYS', symb)
    return b'TSFB' + B(len(body)) + body

open(out + '/Test.tkl', 'wb').write(tsfb(hunk(b'TCES', sect)))
