#!/usr/bin/env python3
"""Synthetic "wii\\0" resource archives (see project/src/lib-wiiresarc.h): a
relative-offset level archive and an absolute-offset sound bank.
usage: mk_wiiresarc.py OUTDIR -> OUTDIR/LEVEL.ARC, OUTDIR/SOUNDS.ARC"""
import os, struct, sys

NAMES = ['alpha', 'beta']
DATA = [(2, b'first payload'), (4, b'second')]


def build(relative):
    count = len(DATA)
    recs_off = 0x100
    names_off = recs_off + 0x20 * count
    strs = b''.join(n.encode() + b'\0' for n in NAMES)
    name_tab = b''
    p = names_off + 4 * count
    for n in NAMES:
        name_tab += struct.pack('>I', p)
        p += len(n) + 1
    base = (p + len(strs) * 0 + 0x3f) & ~0x3f
    out = bytearray(base)
    out[0:4] = b'wii\0'
    struct.pack_into('>4I', out, 4, 0, base if relative else 0x1000, count, 0)
    struct.pack_into('>2I', out, 0x14, names_off, base)
    off = 0
    for i, (t, d) in enumerate(DATA):
        o = off if relative else base + off
        struct.pack_into('>8I', out, recs_off + 0x20 * i, 0, t, len(d), o, i + 1, 0, 0x20, 0)
        off += (len(d) + 15) & ~15
    out[names_off:names_off + len(name_tab)] = name_tab
    q = names_off + len(name_tab)
    out[q:q + len(strs)] = strs
    for t, d in DATA:
        out += d.ljust((len(d) + 15) & ~15, b'\0')
    return bytes(out)


os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'LEVEL.ARC'), 'wb').write(build(True))
open(os.path.join(sys.argv[1], 'SOUNDS.ARC'), 'wb').write(build(False))
