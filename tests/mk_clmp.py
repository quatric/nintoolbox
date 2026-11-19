#!/usr/bin/env python3
"""Synthetic War Drum CLMP model (Manhunt 2 Wii .dff; see project/src/lib-clmp.h): one mesh block with a shared
s16 position/normal array (fraction 14), an s16 texcoord array (fraction 11) and a command list with a material
entry, a vertex-format entry (u8 position/normal/texcoord indices) and one GX triangle display list.
usage: mk_clmp.py OUT.dff"""
import struct, sys
S = bytearray(0x600)
def put(o, b): S[o:o + len(b)] = b
B = lambda *a: struct.pack('>%dI' % len(a), *a)
put(0, b'CLMP' + B(1, 0x600))
b = 0x100
put(b, B(0x18, 0x40, 0x60, 0x70, 0x80) + bytes([1]))
put(b + 0x18, B(0x24) + struct.pack('>HBB', 3, 0x3e, 6) + B(0))        # position descriptor
for i, p in enumerate(((0, 0, 0), (16384, 0, 0), (0, 16384, 0))):
    put(b + 0x24 + 12 * i, struct.pack('>6h', *p, 0, 0, 16384))         # position + normal
put(b + 0x70, B(0x2a) + struct.pack('>HBB', 3, 0x3e, 6) + B(0))        # normal descriptor
put(b + 0x60, B(0x200) + struct.pack('>HBB', 3, 0x3b, 2) + B(0, 0))    # texcoord descriptor
for i, t in enumerate(((0, 0), (2048, 0), (0, 2048))):
    put(b + 0x200 + 4 * i, struct.pack('>2h', *t))
put(b + 0x80, B(0, 0x90) + struct.pack('>H', 3))                        # command list: entries at +0x90, count 3
put(b + 0x90, bytes([1, 0, 0, 0]) + B(5, 0, 0))                         # material 5
put(b + 0xa0, bytes([3, 0, 0, 0]) + B(0x828, 0, 0))                     # u8 position, normal, texcoord 0
put(b + 0xb0, bytes([4, 0, 0, 0]) + B(0, 0x300, 0x20))                  # draw
put(b + 0x300, bytes([0x90, 0, 3]) + bytes([0, 0, 0, 1, 1, 1, 2, 2, 2]))
open(sys.argv[1], 'wb').write(S)
