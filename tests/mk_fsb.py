#!/usr/bin/env python3
"""Synthetic FMOD FSB4 bank (see project/src/lib-fsb.h) with one mono
GCADPCM sample (silent, 28 samples). usage: mk_fsb.py OUTDIR -> OUTDIR/TEST.fsb"""
import os, struct, sys

data = bytes(16)  # two zero DSP frames -> 28 silent samples
hdr = struct.pack('<H30s', 0x7e, b'silent_one.wav')
hdr += struct.pack('<IIIIIiHhHH', 28, len(data), 0, 27, 0x02080000, 22050, 255, 128, 128, 1)
hdr += struct.pack('<ffIHh', 1.0, 10000.0, 0, 255, 128)
hdr = hdr.ljust(0x50, b'\0') + b''.join(struct.pack('>h', c) for c in [0x800] + [0] * 15) + bytes(14)
assert len(hdr) == 0x7e, len(hdr)
out = b'FSB4' + struct.pack('<5I', 1, len(hdr), len(data), 0x40000, 0x10) + bytes(24) + hdr + data
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.fsb'), 'wb').write(out)
