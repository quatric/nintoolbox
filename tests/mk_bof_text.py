"""Synthetic Battle of Giants .bof. Writes <dir>/t.bof; "check <dir>" verifies t.bof.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.bof.d", "t.json")))
        assert j["strings"] == ["Narrator", "NAR", "héllo \"x\""], j
        assert j["index"] == [[3, 4, 1], [9, 5, 1]], j
        print("PASS")
        sys.exit(0)
    s = "Narrator\0NAR\0héllo \"x\"\0".encode("utf-16le")
    out = struct.pack("<III", 1, 3, len(s) // 2) + s + struct.pack("<6I", 3, 4, 1, 9, 5, 1)
    open(os.path.join(sys.argv[1], "t.bof"), "wb").write(out)
