"""Synthetic Spectrobes .HIT. Writes <dir>/t.HIT; "check <dir>" verifies t.HIT.d/collision.obj and groups.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        d = os.path.join(sys.argv[2], "t.HIT.d")
        j = json.load(open(os.path.join(d, "groups.json")))
        assert j["groups"] == [{"name": "GROUND", "quads": 2}], j
        o = open(os.path.join(d, "collision.obj")).read().split("\n")
        v = [l for l in o if l.startswith("v ")]
        assert len(v) == 8 and v[0] == "v 1 0 0" and v[1] == "v 1 2 0" and v[2] == "v 3 2 0" and v[3] == "v 3 0 0", v
        assert "f 1 2 3 4" in o and "f 5 6 7 8" in o and "o GROUND" in o, o
        print("PASS")
        sys.exit(0)

    def quad(x0):
        # corners A(x0,0,0) B(x0,2,0) C(x0+2,2,0) D(x0+2,0,0) stored as 3x4 with D in the last column
        m = [x0, 0, 0, x0 + 2, x0, 2, 0, 0, x0 + 2, 2, 0, 0]
        return struct.pack(">4f", 0.5, 0.5, 0.5, 1.0) + struct.pack(">12f", *m) + b"\0" * 0x50

    q = quad(1) + quad(10)
    out = struct.pack(">4I", 0x0F0F0F0F, 1, 0, 0x50) + struct.pack(">4I", 1, 0x20, 0, 0)
    out += b"GROUND".ljust(0x20, b"\0") + struct.pack(">4I", 2, 0x50, 0x50 + len(q), 0)
    out += q
    open(os.path.join(sys.argv[1], "t.HIT"), "wb").write(out)
