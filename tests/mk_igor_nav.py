"""Synthetic Igor ".nav" (chkf/nav): 3 vertices, 1 triangle. Writes <dir>/t.nav; "check <dir>" verifies t.nav.d/navmesh.obj."""
import os
import struct
import sys

EXPECT = """o navmesh
v 1 0 2
v 3 0 4
v 5 0 6
f 1 2 3
"""

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.nav.d", "navmesh.obj")).read()
        assert got == EXPECT, got
        print("PASS")
        sys.exit(0)
    d = struct.pack(">7I", 0, 1, 4, 0, 0, 3, 1)
    for i, (x, z) in enumerate(((1, 2), (3, 4), (5, 6))):
        d += struct.pack(">3f4fI", x, 0, z, 0, 1, 0, 0, 1) + struct.pack(">I", 0)
    d += struct.pack(">3I3iI", 0, 1, 2, -1, -1, -1, 0)
    out = b"chkf" + struct.pack(">I", 9) + b"nav\0data\0" + d
    open(os.path.join(sys.argv[1], "t.nav"), "wb").write(out)
