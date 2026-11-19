#!/usr/bin/env python3
"""Synthetic SEGA ABDA block (Sakura Wars .abd; see project/src/wszst_cmd/extract/sega_abda.inc): header 0x30 with one section
entry, one ABDT section whose payload starts {hash, 0x40, -1}.
usage: mk_sega_abda.py OUT.abd"""
import struct, sys
sec = b'ABDT' + struct.pack('<III', 0x20, 0x10, 0) + struct.pack('<IIiII', 0x77820d59, 0x40, -1, 0, 6) + bytes(12)
S = bytearray(0x30)
S[0:4] = b'ABDA'
struct.pack_into('<III', S, 4, 0x30 + len(sec), 0x30, 0)
struct.pack_into('<III', S, 0x10, 0x77820d59, 1, len(sec))
struct.pack_into('<II', S, 0x20, 0x30, 0)
open(sys.argv[1], 'wb').write(bytes(S) + sec)
