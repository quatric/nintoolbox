"""Synthetic Igor ".ggm" (chkf/xgm): one skeleton node and one skinned 3-vertex mesh. Writes <dir>/t.ggm; "check <dir>"
verifies t.ggm.d/model.obj and skeleton.json."""
import json
import os
import struct
import sys

EXPECT = """o mesh_0
v 1 2 3
v -4 0.5 0
v 0 0 -8
vt 0 1
vt 1 0
vt 0.5 1.5
vn 0 0.999969 0
vn 0.999969 0 0
vn 0 0 -1
f 1/1/1 2/2/2 3/3/3
"""
LOCAL = "1 0 0 0 0 1 0 0 0 0 1 0 5 6 7 1"
INV = "1 0 0 0 0 1 0 0 0 0 1 0 -5 -6 -7 1"

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.ggm.d", "model.obj")).read()
        assert got == EXPECT, got
        sk = json.load(open(os.path.join(sys.argv[2], "t.ggm.d", "skeleton.json")))
        n = sk["nodes"][0]
        assert n["name"] == "Root" and n["parent"] == -1 and n["local"][12:15] == [5, 6, 7] and n["inverse"][12:15] == [-5, -6, -7], sk
        print("PASS")
        sys.exit(0)
    st = b"xgm\0header\0modeldata\0"
    name = len(st); st += b"Root\0"
    m1 = len(st); st += LOCAL.encode() + b"\0"
    m2 = len(st); st += INV.encode() + b"\0"
    fmt = len(st); st += b"EVTXSTREAMFMT_V12N6I1T4\0"
    prim = len(st); st += b"EPRIM_TRIANGLES\0"
    verts = [(1, 2, 3, (0, 32768, 0), (0, 0)), (-4, 0.5, 0, (32767, 0, 0), (1024, 1024)), (0, 0, -8, (0, 0, -32768), (512, -512))]
    vb = b"".join(struct.pack(">3f3hBBhh", x, y, z, *n, 0, 0, *uv) for x, y, z, n, uv in [(a, b, c, tuple(min(v, 32767) for v in n), uv) for a, b, c, n, uv in verts])
    ib = struct.pack(">3H", 0, 1, 2)
    data = vb + ib
    hdr = struct.pack(">8I", 1, 0, 0, 0, 0, 0, 0, len(data))  # data size at +0x1c
    hdr += struct.pack(">I", 1)  # node count at +0x20
    hdr += struct.pack(">5I", 0, 0xffffffff, name, m1, m2)
    hdr += struct.pack(">IIIIIIIIIIII", 1, fmt, 0, 3, 0x18, 0, prim, 3, len(vb), 0, 0, name)
    out = b"chkf" + struct.pack(">I", len(st)) + st + hdr + data
    open(os.path.join(sys.argv[1], "t.ggm"), "wb").write(out)
