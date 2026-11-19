#!/usr/bin/env python3
"""Synthetic SEGA GTPA text page table (Sakura Wars .gtp; see project/src/wszst_cmd/extract/sega_gtpa.inc): two pages with 24 layout words
and a NUL-terminated message at +0x60. Offsets are relative to 0x10.
usage: mk_sega_gtpa.py OUT.gtp"""
import struct, sys
texts = [b'Hello, world.\nLine two\0', b'Second "page"\0']
hdr = 0x14 + 4 * 2
S = bytearray(hdr)
S[0:4] = b'GTPA'
struct.pack_into('<III', S, 8, 0x10, 0, 0)
struct.pack_into('<I', S, 0x10, 2)
body = []
off = hdr
for i, t in enumerate(texts):
    rec = bytearray(0x60) + t
    rec += bytes((-len(rec)) % 4)
    struct.pack_into('<I', rec, 0, 0x17 + i)
    struct.pack_into('<I', S, 0x14 + 4 * i, off - 0x10)
    body.append(rec)
    off += len(rec)
S += b''.join(body)
struct.pack_into('<I', S, 4, len(S))
open(sys.argv[1], 'wb').write(S)
