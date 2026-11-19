#!/usr/bin/env python3
"""Synthetic unpacked SEGA MSCR script (Sakura Wars .msb; see project/src/wszst_cmd/extract/sega_msb.inc): two entries, one with two argument
words and 4 code bytes, a three word table.
usage: mk_sega_msb.py OUT.msb"""
import struct, sys
body = bytearray(0x20 + 32 + 8 + 4 + 2 + 12)
etab, ne = 0x20, 2
args, code1, code2, wtab = 0x40, 0x48, 0x4c, 0x4e
struct.pack_into('<8I', body, 0, 0x1234, 0x5678, wtab, 3, etab, ne, 0, 0)
struct.pack_into('<4I', body, etab, 7, args, 2, code1)
struct.pack_into('<4I', body, etab + 16, 8, 0, 0, code2)
struct.pack_into('<II', body, args, 11, 22)
body[code1:code1 + 4] = bytes([1, 2, 3, 4])      # words 0x0201 0x0403, not opcodes (the listing prints them as hex)
body[code2:code2 + 2] = bytes([5, 6])
struct.pack_into('<III', body, wtab, 100, 200, 300)
hdr = bytearray(0x20)
hdr[0:4] = b'MSCR'; struct.pack_into('<III', hdr, 4, len(body) + 0x20 - 0x30, 0x20, 0)
open(sys.argv[1], 'wb').write(bytes(hdr) + bytes(body))
