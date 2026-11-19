#!/usr/bin/env python3
"""Synthetic SEGA MPLN graphics set with one MDCG cell (Sakura Wars .GRD; see project/src/wszst_cmd/extract/sega_grd.inc): 256 colour palette, one textured quad record
(entry j = (j, 255 - j, 7, 0x80)) and a Melt packed HTEX holding an HTSF with one 256x256 kind 0x64 PVRT (index byte i = i & 255).
usage: mk_sega_grd_atlas.py OUT.GRD"""
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
pal = b''.join(bytes((j, 255 - j, 7, 0x80)) for j in range(256))
pvrt = b'PVRT' + struct.pack('<IBBHHH', 0x10008, 9, 0x64, 0, 256, 256) + bytes(16) + bytes(i & 255 for i in range(65536))
htsf = b'HTSF' + struct.pack('<III', 0x10 + len(pvrt), 0x10, 0) + pvrt
htex = pack(htsf)
# MDCG: chunk header, then body: 0x30 byte head whose word 5 (+0x14) = offset of the HTEX chunk inside the body
quad = struct.pack('<II3f', 0x0fc00a, 0xfc, 224.0, -34.5, 93.5) + struct.pack('<f', 0) + b''.join(struct.pack('<4f', *v) for v in ((192, -68.5, 0, 0), (256, -68.5, 0.25, 0), (256, 0, 0.25, 0.25), (192, 0, 0, 0.25))) + bytes(8)
assert len(quad) == 96
mbody = bytearray(0x30)
struct.pack_into('<I', mbody, 0x14, 0x30 + len(quad))
mbody += quad + htex
mdcg = b'MDCG' + struct.pack('<III', len(mbody), 0x10, 0) + bytes(mbody)
body = bytearray(0x30)
struct.pack_into('<II', body, 0, 0x30, 0x30 + len(pal))
struct.pack_into('<HHHH', body, 8, 17, 1, 256, 256)
struct.pack_into('<II', body, 0x28, 0x902, len(pal))
body += pal + mdcg + b'EOFC' + struct.pack('<III', 0, 0x10, 0)
open(sys.argv[1], 'wb').write(b'MPLN' + struct.pack('<III', len(body), 0x10, 0) + bytes(body))
