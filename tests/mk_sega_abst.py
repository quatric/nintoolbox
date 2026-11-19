#!/usr/bin/env python3
"""Synthetic SEGA ABST asset schedule (Sakura Wars .asd; see project/src/wszst_cmd/extract/sega_abst.inc): header 0x30 with a
zero-terminated chunk offset list, one ASAD asset descriptor named "evmap05_01_00" and one ASSC block.
usage: mk_sega_abst.py OUT.asd"""
import struct, sys
S = bytearray(0x30 + 0x50 + 0x30)
S[0:4] = b'ABST'
struct.pack_into('<III', S, 4, len(S), 0x30, 0)
struct.pack_into('<III', S, 0x24, 0x30, 0x80, 0)    # chunk offsets, zero terminated (the header ends at 0x30)
S[0x30:0x34] = b'ASAD'; struct.pack_into('<II', S, 0x38, 0x50, 0)
S[0x40:0x40 + 13] = b'evmap05_01_00'
struct.pack_into('<II', S, 0x60, 1, 0x44)
S[0x80:0x84] = b'ASSC'; struct.pack_into('<II', S, 0x84, 0x30, 0x50)
struct.pack_into('<I', S, 0x90, 7)
open(sys.argv[1], 'wb').write(S)
