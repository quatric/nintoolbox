#!/usr/bin/env python3
"""Synthetic Virtua Tennis "PTEX" bundle (see project/src/wszst_cmd/extract/ptex.inc):
one 8x8 CMPR+I4-alpha texture (kind 2) and one plain 8x8 CMPR texture (kind 0).
usage: mk_ptex.py OUTDIR -> OUTDIR/TEST.dat"""
import os, struct, sys

blk = struct.pack('>HHI', 0xffff, 0x0000, 0)   # white block
cmpr = blk * 4
alpha = bytes([0xf0, 0x0f, 0xff, 0x00] * 8)
tex = [(0x11111111, 2, cmpr + alpha), (0x22222222, 0, cmpr)]
out = bytearray(0x10 + 0x0c + 0x20 * len(tex))
off = len(out) - 0x10
body = b''
rec = b''
for h, kind, data in tex:
    rec += struct.pack('>8I', h, kind, 0x00080008, 0x00080008, off + len(body), 0x3f800000, 0x3f800000, 0)
    body += data
out[0:4] = b'PTEX'
struct.pack_into('>6I', out, 4, 0, 0, 0x44332211, 0, 0, len(tex))
out[0x1c:0x1c + len(rec)] = rec
out += body
struct.pack_into('>2I', out, 4, len(out), len(out) - 0x10)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.dat'), 'wb').write(bytes(out))
