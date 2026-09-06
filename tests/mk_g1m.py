"""Write a minimal Koei Tecmo G1M: one submesh, four vertices, one strip.

Mirrors the retail layout -- chunk list, G1MG with its section table, and
the three geometry sections -- closely enough to exercise the reader's
bounds and range checks without shipping cart data.
"""
import struct, sys

def sec(t, count, payload):
    return struct.pack("<III", t, 12 + len(payload), count) + payload

verts = b"".join(struct.pack("<3f", x, y, 0.0) for x, y in
                 ((0, 0), (1, 0), (0, 1), (1, 1)))
vbuf = sec(0x00010004, 1, struct.pack("<IIII", 0, 12, 4, 0) + verts)

# A four-index strip: two triangles, no degenerates.
idx = struct.pack("<4H", 0, 1, 2, 3)
ibuf = sec(0x00010007, 1, struct.pack("<III", 4, 16, 0) + idx)

# Submesh: vertex start/count at 0x28, index start/count at 0x30.
sub = bytearray(0x38)
struct.pack_into("<IIII", sub, 0x28, 0, 4, 0, 4)
smesh = sec(0x00010008, 1, bytes(sub))

body = struct.pack("<4s4sI", b"3DS\0"[::-1], b"\0\0\0\0", 0)[:8]
g1mg_body = b"3DS\0" + struct.pack("<I", 0)
g1mg_body += struct.pack("<6f", -1, -1, -1, 2, 2, 2)      # bounding box
g1mg_body += struct.pack("<I", 3)                          # section count
g1mg_body += vbuf + ibuf + smesh
g1mg = b"GM1G" + b"4400" + struct.pack("<I", 12 + len(g1mg_body)) + g1mg_body

g1m = b"_M1G" + b"0036" + struct.pack("<I", 0x18 + len(g1mg)) \
    + struct.pack("<III", 0x18, 0, 1) + g1mg
open(sys.argv[1], "wb").write(g1m)
