#!/usr/bin/env python3
"""Synthetic Muramasa .ftx: FCMP (all-literal LZSS) around an FTEX with one 8x4 I8 TPL. usage: mk_fcmpftex.py OUTDIR -> OUTDIR/t.ftx"""
import os, struct, sys

w, h = 8, 4
pixels = bytes(0x80 for _ in range(w * h))
data_off = 0x40
tpl = struct.pack('>III', 0x0020af30, 1, 0x0c) + struct.pack('>II', 0x14, 0)
tpl += struct.pack('>HHIIIIIIfBBBB', h, w, 1, data_off, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = tpl.ljust(data_off, b'\0') + pixels
chunk = (b'FTX0' + struct.pack('<III', 0x40 + len(tpl), 0x40, 0)).ljust(0x40, b'\0') + tpl
rec = b't.tpl'.ljust(0x20, b'\0') + struct.pack('<III', 0, len(tpl), 0).ljust(0x10, b'\0')
hdr = (b'FTEX' + struct.pack('<III', 0, 0x80, 1)).ljust(0x20, b'\0') + rec
body = hdr.ljust(0x80, b'\0') + chunk
body = body[:4] + struct.pack('<I', len(body)) + body[8:]
lz = b''
for i in range(0, len(body), 8):
    blk = body[i:i + 8]
    lz += bytes([(1 << len(blk)) - 1]) + blk
# trailing zero padding as 40 blocks of eight length-18 matches from the never-written ring offset 0x800
# (the container needs decompressed size >= compressed size)
lz += (b'\x00' + bytes([0x00, 0x80 | 15]) * 8) * 40
out = b'FCMP' + struct.pack('<II', len(body) + 40 * 144, 0x12340000) + lz
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 't.ftx'), 'wb').write(out)
