#!/usr/bin/env python3
# Synthetic EA DBPF 2.0 package t.package: a name map, a RefPack-compressed 4x4 RGBA8 texture and a stored blob.
import struct, sys, os
def refpack(d):
    assert len(d) % 4 == 0
    out = bytearray(b'\x10\xfb' + len(d).to_bytes(3, 'big'))
    for i in range(0, len(d), 112):
        c = d[i:i + 112]
        out += bytes([0xe0 | ((len(c) - 4) >> 2)]) + c
    return bytes(out) + b'\xfc'
pix = bytes((i * 7) & 255 for i in range(64))
hdr = struct.pack('>IIIII', 0x14fe0149, 0x10000, 64, 0, 0) + bytes(8) + struct.pack('>HHI', 4, 4, 6) + bytes(0x4c - 0x24)
tex = hdr + pix
tex_c = refpack(tex)
nm = b'tex1'
nmap = struct.pack('<IIIII', 1, 1, 0x1234, 0, len(nm)) + nm
blob = b'hello dbpf'
members = [(0x0166038c, 0, 0, 1, nmap, len(nmap)), (0x00b2d882, 0, 0, 0x1234, tex_c, len(tex)), (0x2019972, 0, 0, 7, blob, len(blob))]
data = bytearray(); ents = bytearray()
for t, g, hi, lo, d, mem in members:
    off = 0x60 + len(data)
    data += d
    ents += struct.pack('<8I', t, g, hi, lo, off, len(d) | 0x80000000, mem, 0xffff0001)
io = 0x60 + len(data)
index = struct.pack('<I', 0) + bytes(ents)
head = bytearray(0x60)
head[0:4] = b'DBPF'
struct.pack_into('<II', head, 4, 2, 0)
struct.pack_into('<I', head, 0x24, len(members))
struct.pack_into('<I', head, 0x2c, len(index))
struct.pack_into('<I', head, 0x3c, 3)
struct.pack_into('<I', head, 0x40, io)
open(os.path.join(sys.argv[1], 't.package'), 'wb').write(bytes(head) + bytes(data) + index)
