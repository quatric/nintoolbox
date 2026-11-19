#!/usr/bin/env python3
"""Synthetic SEGA MPLN graphics set (Sakura Wars .GRD; see project/src/wszst_cmd/extract/sega_grd.inc): 640x624, a 4 colour palette
and one MNCA table with two rectangles, then EOFC.
usage: mk_sega_grd.py OUT.GRD"""
import struct, sys
pal = bytes([0, 0, 0, 0, 1, 2, 3, 0x40, 9, 8, 7, 0x80, 255, 255, 255, 0x80])
body = bytearray(0x30)
soff = 0x30 + len(pal)
struct.pack_into('<II', body, 0, 0x30, soff)
struct.pack_into('<HHHH', body, 8, 17, 1, 640, 624)
struct.pack_into('<II', body, 0x28, 0x902, len(pal))
body += pal
mnca = struct.pack('<III', 1, 2, 2) + struct.pack('<4H', 1, 2, 3, 4) + struct.pack('<4H', 5, 6, 7, 8)
body += b'MNCA' + struct.pack('<III', len(mnca), 0x10, 0) + mnca
body += b'EOFC' + struct.pack('<III', 0, 0x10, 0)
open(sys.argv[1], 'wb').write(b'MPLN' + struct.pack('<III', len(body), 0x10, 0) + bytes(body))
