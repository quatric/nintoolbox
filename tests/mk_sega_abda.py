#!/usr/bin/env python3
"""Synthetic SEGA ABDA block (Sakura Wars .abd; see project/src/wszst_cmd/extract/sega_abda.inc): header 0x30 with one section
entry, one ABDT section whose payload starts {hash, 0x40, -1}.
usage: mk_sega_abda.py OUT.abd"""
import struct, sys
payload = bytearray(0x4c)
struct.pack_into('<IIi', payload, 0, 0x77820d59, 0x40, -1)
struct.pack_into('<IIII', payload, 0x24, 2, 0x44, 0x50, 0x5c)         # two blocks (offsets relative to the section start) and the end offset
sec = b'ABDT' + struct.pack('<III', len(payload), 0x10, 0) + bytes(payload)
# POF0 chunk after the section: slots at file offsets 0x20 (section entry) and 0x30 + 0x38 (first block offset): deltas 8 and 0x12 words -> bytes 0x48, 0x52
pof = b'POF0' + struct.pack('<III', 0x10, 0x10, 0) + struct.pack('<I', 8) + bytes([0x48, 0x52, 0, 0]) + bytes(4)
S = bytearray(0x30)
S[0:4] = b'ABDA'
struct.pack_into('<III', S, 4, 0x30 + len(sec) + len(pof), 0x30, 0)
struct.pack_into('<III', S, 0x10, 0x77820d59, 1, 0x30 + len(sec))      # hash, section count, file offset of the POF0 chunk
struct.pack_into('<II', S, 0x20, 0x30, 0)
open(sys.argv[1], 'wb').write(bytes(S) + sec + pof)
