#!/usr/bin/env python3
"""Synthetic Heavy Iron .ho package (see project/src/lib-ho.h): one asset layer
with two assets and a PSLD debug-name layer. usage: mk_ho.py OUTDIR -> OUTDIR/TEST.ho"""
import os, struct, sys

B = lambda *a: struct.pack('>%dI' % len(a), *a)
assets = [(0x0000000600000001, 0x11223344, b'hello ho\n'), (0x0000000600000002, 0x55667788, bytes(range(40)))]
names = ['first_asset', 'second_asset']

# layer 0: asset table + data
toc = B(len(assets), 0xffffffff) + b't' * 24
data = b''
ents = b''
off = 0x800
for aid, typ, d in assets:
    padded = (len(d) + 15) & ~15
    ents += B(padded, off, len(d), 4) + struct.pack('>Q', aid) + B(typ, 1)
    data += d.ljust(padded, b'3')
    off += padded
layer0 = (toc + ents).ljust(0x800, b'3') + data
# layer 1: names
esz = 0x40
name_ents = b''
for (aid, typ, d), n in zip(assets, names):
    e = struct.pack('>Q', aid) + B(0x20, 0, 0xffffffff) + b'\0' * 12 + n.encode() + b'\0'
    name_ents += e.ljust(esz, b'3')
layer1 = B(esz, esz).ljust(0x40, b'3') + name_ents

def psl(tag, n, slices):
    return tag + B(0x10 + 16 * len(slices), n, 0) + b''.join(B(*s) for s in slices)
sect_hdr = 0x20 + 0x40 * 2
meta0 = sect_hdr
psl0 = b'PSL\0' + B(0x20 + 0, 1, 0) + B(0, 0, 0x40 + 0x20 * len(assets), 0xffffffff)
meta1 = meta0 + len(psl0)
psld = b'PSLD' + B(0x20, len(assets), 0x40, 0xffffffff, 0xffffffff, 0)
sect = bytearray(b'SECT' + B(2, 0, 0, 0, 0, 0)[:28])
struct.pack_into('>I', sect, 4, 2)
sect = bytes(sect).ljust(0x20, b'\0')
def entry(typ, sector, size, meta):
    e = typ + B(0x00000133, 0, 0, 0, 0xffffffff, 0, 0)[:0] 
    e = typ.ljust(4, b' ') + B(0x33, 0, 0, 0, 0, 0xffffffff, sector, size, size, 0xffffffff, 0, 0xffffffff, 1, meta, 0)
    return e[:0x40].ljust(0x40, b'\0')
sect += entry(b'P', 3, len(layer0), meta0) + entry(b'PD', 3 + (len(layer0) + 0x7ff) // 0x800, len(layer1), meta1)
sect += psl0 + psld
sect = sect.ljust(0x800, b'3')

hdr = bytearray(0x800)
hdr[0:4] = b'HEL\x1a'
struct.pack_into('>I', hdr, 4, 1)
mast = bytearray(0x20 + 0x40)
mast[0:4] = b'MAST'
struct.pack_into('>I', mast, 4, 1)
mast[0x20:0x24] = b'SECT'
struct.pack_into('>I', mast, 0x20 + 0x1c, 2)  # SECT at sector 2 (0x1000)
mast = bytes(mast).ljust(0x800, b'\0')
out = bytes(hdr) + mast + sect + layer0.ljust((len(layer0) + 0x7ff) & ~0x7ff, b'3') + layer1
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.ho'), 'wb').write(out)
