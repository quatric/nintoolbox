#!/usr/bin/env python3
"""Synthetic Muramasa .nms: FCMP (all-literal LZSS plus zero padding matches) around an NMSB sheet with two strings
("Hello" and "a" newline "b"). usage: mk_muranms.py OUTDIR -> OUTDIR/t.nms"""
import os, struct, sys

strs = [b'Hello', b'a\nb']
pool = b''.join(s + b'\0' for s in strs)
body = b'NMSB' + struct.pack('<IIIIII', 0, 0x20, 0, 0, len(strs), 0) + bytes(4)
body = body[:0x20].ljust(0x20, b'\0') + bytes(4 * len(strs)) + pool
body = body[:4] + struct.pack('<I', len(body)) + body[8:]
body = body.ljust((len(body) + 7) & ~7, b'\0')   # whole literal blocks only
lz = b''
for i in range(0, len(body), 8):
    blk = body[i:i + 8]
    lz += bytes([(1 << len(blk)) - 1]) + blk
lz += (b'\x00' + bytes([0x00, 0x80 | 15]) * 8) * 40
out = b'FCMP' + struct.pack('<II', len(body) + 40 * 144, 0x12340000) + lz
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 't.nms'), 'wb').write(out)
