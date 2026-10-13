#!/usr/bin/env python3
"""Synthetic pop'n music files: t.sen (two UTF-16BE strings) and t.aept (AEPT header + an 8x4 I8 TPL). usage: mk_popn.py OUTDIR"""
import os, struct, sys

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
strs = ['Hello', 'Back\nHome']
blobs = [s.encode('utf-16-be') + b'\0\0' for s in strs]
off = 12 + 4 * len(strs)
offs = []
for b in blobs:
    offs.append(off)
    off += len(b)
sen = b'SEN\0' + struct.pack('>II', len(strs), off) + b''.join(struct.pack('>I', o) for o in offs) + b''.join(blobs)
open(os.path.join(d, 't.sen'), 'wb').write(sen)

w, h = 8, 4
tpl = struct.pack('>III', 0x0020af30, 1, 0x0c) + struct.pack('>II', 0x14, 0)
tpl += struct.pack('>HHIIIIIIfBBBB', h, w, 1, 0x40, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = tpl.ljust(0x40, b'\0') + bytes(w * h)
aept = (b'AEPT' + struct.pack('>I', 2)).ljust(0x40, b'\0') + tpl
open(os.path.join(d, 't.aept'), 'wb').write(aept)
