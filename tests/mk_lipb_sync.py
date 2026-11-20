"""Synthetic Batman .lipb. Writes <dir>/t.lipb; "check <dir>" verifies t.lipb.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.lipb.d", "t.json")))
        assert j["sound"] == "a_1_001_x.ogg" and j["keys"] == [[0, 3], [0.5, 2], [1.25, 6]], j
        print("PASS")
        sys.exit(0)
    out = struct.pack(">I", 0) + b"a_1_001_x.ogg".ljust(60, b"\0") + struct.pack(">4I", 0, 3, 0x4C, 0)
    out = out[:0x44] + struct.pack(">II", 3, 0x4C)
    for t, v in [(0.0, 3), (0.5, 2), (1.25, 6)]:
        out += struct.pack(">fI", t, v)
    open(os.path.join(sys.argv[1], "t.lipb"), "wb").write(out)
