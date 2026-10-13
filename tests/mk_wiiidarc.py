#!/usr/bin/env python3
# Synthetic Nippon Ichi WIIIDARC sound archive (Phantom Brave, Wii) with two mono .wsd members sharing a name as t.dat.
import struct, sys, os
d = sys.argv[1]
def wsd(nib, fill):
    have = (nib + 1) // 2
    dsp = struct.pack('>IIIHHIII', nib * 2 - 2, nib, 22050, 0, 0, 2, nib - 1, 2) + bytes(0x60 - 0x1c)
    return struct.pack('>IIII', 0x20, 0x80 + have, 0x60 + have, 0) + bytes(16) + dsp + bytes([fill]) * have
members = [wsd(32, 0x11), wsd(21, 0x22)]
n = len(members)
tab = (0x20 + 2 * n + 31) & ~31
off = (tab + 0x30 * n + 0xff) & ~0xff
f = bytearray(off)
f[0:8] = b'WIIIDARC'
struct.pack_into('>I', f, 8, n)
struct.pack_into('>2H', f, 0x20, 1, 2)
for i, m in enumerate(members):
    e = tab + 0x30 * i
    f[e:e + 5] = b'a.wsd'
    struct.pack_into('>II', f, e + 0x28, len(m), len(f))
    f += m
    f += bytes(-len(f) % 0x100)
open(os.path.join(d, 't.dat'), 'wb').write(bytes(f))
