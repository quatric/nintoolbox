#!/usr/bin/env python3
"""Synthetic "waud" sounds: s16 stereo 44100 (.wtss) and u8 mono 11025 (.wtxs). usage: mk_waud.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def waud(bps, ch, flag, rate, data):
    h = b'waud' + bytes([bps, ch, flag, 0]) + struct.pack('>I', rate << 8) + struct.pack('>HH', len(data) >> 8, 0x1010)
    return h.ljust(32, b'\0') + data
open(os.path.join(d, 's.wtss'), 'wb').write(waud(2, 2, 1, 44100, struct.pack('>hhhh', 0x1234, -2, 0x0102, 0x7fff)))
open(os.path.join(d, 'e.wtxs'), 'wb').write(waud(1, 1, 0, 11025, bytes([0x80, 0x90, 0x70])))
