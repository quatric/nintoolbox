#!/usr/bin/env python3
# Synthetic Krome model t.mdl / t.mdg: one chunk with one triangle strip of three 9 byte records, one colour, three
# positions and three UVs.
import struct, sys, os
d = sys.argv[1]
al = lambda x: (x + 31) & ~31
mdl = b'MDL3' + bytes(0xfc) + b'M_t\0T_t\0'
open(os.path.join(d, 't.mdl'), 'wb').write(mdl)
recs = b''
for i, (n, uv) in enumerate((((0, 0, 64), 0), ((0, 0, 64), 1), ((0, 0, 64), 2))):
    recs += struct.pack('>HbbbHH', i, *n, 0, uv)
strips = bytes([0x98]) + struct.pack('>H', 3) + recs
s0 = 0x40
send = s0 + len(strips)
tab = al(send)
f = bytes(0x20) + struct.pack('>4HHH', 9, 9, 0, 1, 0xffff, 0) + bytes(4) + struct.pack('>4I', tab - s0, 0x20, 0x40, 0x20)
g = bytearray(b'MDG3' + b'\xff' * 28) + f[0x20:] + strips
g += bytes(tab - len(g)) + bytes([255, 255, 255, 255]) + bytes(0x1c)
g += b''.join(struct.pack('>3f', *p) for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0))) + bytes(0x40 - 36)
g += b''.join(struct.pack('>hh', *u) for u in ((0, 0), (4096, 0), (0, 4096))) + bytes(0x20 - 12)
open(os.path.join(d, 't.mdg'), 'wb').write(g)
