#!/usr/bin/env python3
# Synthetic Dragon Quest X package t.trb: one 4x2 palette-indexed "SEDBtxb" texture (palette entry 5 = A 0x80,
# B 0x30, G 0x20, R 0x10), one "SEDBshd" block and a pool with two names.
import struct, sys, os
d = sys.argv[1]
w, h = 4, 2
pal = bytearray(1024)
pal[5 * 4:5 * 4 + 4] = bytes((0x80, 0x30, 0x20, 0x10))
dds = b'DDS ' + struct.pack('>6I', 0x7c, 0x81007, h, w, w * h, 0) + bytes(0x80 - 28)
dds = bytearray(dds)
struct.pack_into('>I', dds, 0x4c, 0x20)
struct.pack_into('>I', dds, 0x50, 0x20)
struct.pack_into('>I', dds, 0x58, 8)
txb = b'SEDBtxb\0' + struct.pack('<3I', 1, 0x200, len(dds) + 1024 + w * h) + bytes(12) + bytes(dds) + bytes(pal) + bytes((5,) * (w * h))
shd = b'SEDBshd\0' + struct.pack('<3I', 15, 0x500, 0x40) + bytes(12) + bytes(0x20)
txb += bytes(-len(txb) % 8)
pool = b'F00\\e_def.wii\0F01\\tex.wii\0'
recs = [(0, 0, len(txb), 2), (1, len(txb), len(shd), 5)]
data = txb + shd
base = 0x30 + 16 * len(recs)
out = b'SEDBRES ' + struct.pack('<2I', 4001, 0x300200) + struct.pack('<I', 0) + bytes(12)
out += struct.pack('<3I', len(recs), len(data), 2) + b'brt\0'
for r in recs:
    out += struct.pack('<4I', *r)
out += data + pool
out = out[:0x10] + struct.pack('<I', len(out)) + out[0x14:]
open(os.path.join(d, 't.trb'), 'wb').write(out)
