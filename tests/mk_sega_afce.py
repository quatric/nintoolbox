#!/usr/bin/env python3
"""Synthetic SEGA AFCE face set (Sakura Wars MRS_KAO_DUMMY.GRD; see project/src/wszst_cmd/extract/sega_grd.inc): header, a 16 byte table and two
Melt packed HTEX chunks (literals only): a 0x40 byte "HTSF" container and a 128x128 palette face texture (palette entry j = (j, 255 - j, 7, 0x80), index byte i = i & 255).
usage: mk_sega_afce.py OUT.GRD"""
import struct, sys
def pack(htsf):
    stream = bytearray(); st = {'nb': 0, 'fpos': 0}
    def flag(bit):
        if st['nb'] == 0:
            st['fpos'] = len(stream); stream.append(0); st['nb'] = 8
        if not bit:
            stream[st['fpos']] |= 1 << (8 - st['nb'])
        st['nb'] -= 1
    for b in htsf:
        flag(1); stream.append(~b & 0xff)
    flag(0); flag(1); stream.extend([0xff, 0xff])
    htex = bytearray(0x20)
    htex[0:4] = b'HTEX'; struct.pack_into('<III', htex, 4, len(stream) + 0x10, 0x10, 0x400)
    struct.pack_into('<II', htex, 0x10, len(htsf), 0x8000); htex[0x18:0x20] = b'\xff' * 8
    return bytes(htex) + bytes(stream)
junk = b'HTSF' + struct.pack('<III', 0x30, 0x10, 0) + bytes(range(0x30))
pal = b''.join(bytes((j, 255 - j, 7, 0x80)) for j in range(256))
face = b'HTSF' + struct.pack('<III', 0, 0x10, 0) + b'GBIX' + struct.pack('<III', 8, 0, 0) + b'PVRT' + struct.pack('<IBBHHH', 0x4408, 9, 0x6c, 0, 128, 128) + pal + bytes(i & 255 for i in range(16384))
chunks = pack(junk) + pack(face)
table = struct.pack('<4I', 2, 0x10002, 1, 0x10001)
body = struct.pack('<4I', 4, 0x20, 0, 0) + table
S = b'AFCE' + struct.pack('<III', len(body) + len(chunks), 0x10, 0) + body + chunks
open(sys.argv[1], 'wb').write(S)
