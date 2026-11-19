#!/usr/bin/env python3
"""Synthetic SEGA HMOT motion (Sakura Wars: So Long, My Love; see project/src/wszst_cmd/extract/sega_hmot.inc): one bone with
rotation component 0 (a 3-sample s16 curve, binary point 14) and translation x (static, base 1.25).
usage: mk_hmot.py OUT.hmot"""
import struct, sys
S = bytearray(0xa0)
P = lambda o, fmt, *a: struct.pack_into('<' + fmt, S, o, *a)
S[0:4] = b'HMOT'; P(4, 'II', 0x80, 0x20)
S[0x20:0x24] = b'KFMO'; P(0x24, 'II', 0x70, 0x10)
P(0x30, 'IHHfI', 0x80000003, 1, 1, 2.0, 0x30)      # flags, bones, 1, frames, bone table offset
P(0x50, 'IIII', 0, 0x21 << 24, 0x40, 0x48)          # mask: rot0 + tx
P(0x60, 'II', 0x50, 0)                              # curve headers: rot0 present, tx static
P(0x68, 'ff', 0.5, 1.25)                            # base values
S[0x70:0x74] = bytes([1, 2, 14, 0]); P(0x7c, 'I', 0x70)
P(0x90, '3h', 16384, 0, -8192)
open(sys.argv[1], 'wb').write(S)
