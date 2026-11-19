#!/usr/bin/env python3
"""Synthetic SEGA GLIP lip-sync table (Sakura Wars .cnk; see project/src/wszst_cmd/extract/sega_glip.inc): two entries with
offsets relative to 0x10.
usage: mk_sega_glip.py OUT.cnk"""
import struct, sys
S = bytearray(0x18 + 16 + 5)
S[0:4] = b'GLIP'
struct.pack_into('<IIII', S, 4, len(S), 0x10, 0, 2)
struct.pack_into('<I', S, 0x14, 8)
struct.pack_into('<IIII', S, 0x18, 0x16000, 0x18 + 16 - 0x10, 0x16001, 0x18 + 16 - 0x10 + 3)
S[0x28:0x2d] = bytes([0x2e, 0x46, 0x84, 0x45, 0x26])
open(sys.argv[1], 'wb').write(S)
