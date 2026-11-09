import struct, sys, os
d = sys.argv[1]
# MDMN motion: 2 nodes, channels pos (bit0) + rot (bit5); node 1 has 2 rot keys, node 0 one pos key
pos = struct.pack(">Ifff", 0, 1.0, 2.0, 3.0)
rot = struct.pack(">HhhhHhhh", 0, 0x4000, 0, 0, 1, 0x2000, 0, 0)
base = 0x14
poff = 0xc
roff = 0xc + len(pos)
mdata_off = 0xc + len(pos) + len(rot)
nodes = struct.pack(">IIII", poff, 0, 1, 0) + struct.pack(">IIII", 0, roff, 0, 2)
body = struct.pack(">IIHH", mdata_off, 5, 0x21, 2) + pos + rot + nodes
raw = b"MDMN" + struct.pack(">I", len(body)) + body
open(os.path.join(d, "t.njm"), "wb").write(raw)
