#!/usr/bin/env python3
"""Synthetic Sumo Digital .stz container (see project/src/lib-sumostz.h): a
minimal big-endian header wrapping a zlib stream that inflates to a small
FourCC-tagged ("PTEX") blob.
usage: mk_sumostz.py OUTDIR -> OUTDIR/T.stz"""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

PAYLOAD_OFFSET = 0x48
inner = b'PTEX' + bytes((i * 5) & 0xff for i in range(256))
co = zlib.compressobj(9)  # level 9 -> 78 DA header, matching every retail sample
comp = co.compress(inner) + co.flush()

header = bytearray(PAYLOAD_OFFSET)
struct.pack_into('>I', header, 0x00, PAYLOAD_OFFSET)
struct.pack_into('>I', header, 0x28, len(inner))
struct.pack_into('>I', header, 0x2c, len(comp))
struct.pack_into('>I', header, 0x30, len(comp))
struct.pack_into('>I', header, 0x3c, PAYLOAD_OFFSET + len(comp))

with open(os.path.join(out, 'T.stz'), 'wb') as f:
    f.write(bytes(header) + comp)
