"""Synthetic IO Interactive Glacier ".prm" (Mini Ninjas): header chunk, a non-mesh chunk, an index buffer, a vertex buffer
and a mesh descriptor. Writes <dir>/t.prm; "check <dir>" verifies t.prm.d/model.obj."""
import os
import struct
import sys

EXPECT = """o mesh_4_m7
v 1 2 3
v -4 0.5 0
v 0 0 -8
vt 0 1
vt 1 0
vt 0.5 1.5
vn 0 1 0
vn 1 0 0
vn 0 0 -1
f 1/1/1 2/2/2 3/3/3
"""

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.prm.d", "model.obj")).read()
        assert got == EXPECT, got
        print("PASS")
        sys.exit(0)
    verts = [(1, 2, 3, (0, 127, 0), (0, 0)), (-4, 0.5, 0, (127, 0, 0), (512, 512)), (0, 0, -8, (0, 0, -127), (256, -256))]
    vb = b"".join(struct.pack(">3fb3bhh", x, y, z, 0, *n, *uv) for x, y, z, n, uv in verts)
    ib = struct.pack(">HH3H", 1, 3, 0, 1, 2)
    chunks = [b"\0" * 16, b"\x89\x01" + b"\0" * 0x3e, ib + b"\0" * 6, vb + b"\0" * 4, struct.pack(">IIHHI", 3, 3, 7, 2, 2)]
    body, table, off = b"", [], 0
    for c in chunks:
        table.append((off, len(c)))
        body += c
        off += len(c)
    hdr = struct.pack(">IIII", len(body), len(chunks), len(body), 0)
    body = hdr + body[16:]
    out = body + b"".join(struct.pack(">IIII", o, s, 1, 0) for o, s in table)
    open(os.path.join(sys.argv[1], "t.prm"), "wb").write(out)
