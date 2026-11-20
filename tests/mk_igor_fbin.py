"""Synthetic Igor ".fbin"/".sfbin" font tables. Writes <dir>/t.fbin and <dir>/t.sfbin; "check <dir>" verifies the JSON."""
import json
import os
import struct
import sys


def build(sfbin):
    name = b"fonts/T/t\0"
    out = b"".join(struct.pack("<I", c) for c in b"FRITE") + struct.pack("<III", 18, 1, len(name)) + name
    out += struct.pack("<II", 2, 0)
    for i in range(2):
        out += struct.pack("<II", i, 0) if sfbin else struct.pack("<iiiii", 1 + i, 2, 3, 4, 0)
    out += struct.pack("<II", 65, 2)
    out += struct.pack("<II", 7, 1) + struct.pack("<Iii", 0, -1, -2)
    out += struct.pack("<II", 9, 2) + struct.pack("<Iii", 1, 3, 4) + struct.pack("<Iii", 0, 5, 6)
    return out


if __name__ == "__main__":
    d = sys.argv[2] if sys.argv[1] == "check" else sys.argv[1]
    if sys.argv[1] == "check":
        for ext, rect in (("fbin", [1, 2, 3, 4, 0]), ("sfbin", [0, 0])):
            j = json.load(open(os.path.join(d, "t." + ext + ".d", "t.json")))
            assert j["name"] == "fonts/T/t" and len(j["sprites"]) == 2 and j["sprites"][0] == rect, j
            g = j["glyphs"]
            assert [x["code"] for x in g] == [65, 66] and g[0]["advance"] == 7 and g[1]["sprites"] == [[1, 3, 4], [0, 5, 6]], g
        print("PASS")
        sys.exit(0)
    for ext in ("fbin", "sfbin"):
        open(os.path.join(d, "t." + ext), "wb").write(build(ext == "sfbin"))
