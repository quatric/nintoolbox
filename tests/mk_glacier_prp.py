"""Synthetic Glacier "IOPacked v0.1" ".prp": two names, a container with a string, an array of two floats and an int. Writes <dir>/t.prp; "check <dir>" verifies t.prp.d/t.txt."""
import os
import struct
import sys

EXPECT = """container 1
string 0 "Position"
array 2 [
  float 1
  float 2.5
]
key 1 "Flag"
bool 1
int32 -3
"""

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.prp.d", "t.txt")).read().split("\n", 1)[1]
        assert got == EXPECT, got
        print("PASS")
        sys.exit(0)
    names = b"Position\0Flag\0"
    head = b"IOPacked v0.1\0" + b"\x01\0\0\0\x0d\0\0\0\0" + struct.pack(">I", 2) + struct.pack(">I", len(names))
    assert len(head) == 0x1f, len(head)
    s = struct.pack(">BI", 4, 1) + struct.pack(">BI", 0x0c, 0) + struct.pack(">BI", 1, 2) + struct.pack(">BfBf", 0x0a, 1.0, 0x0a, 2.5) + b"\x7c"
    s += struct.pack(">BI", 0x0e, 1) + b"\x06\x01" + struct.pack(">Bi", 9, -3) + b"\x7f"
    out = head + names + b"\0\0\0\0" + s
    open(os.path.join(sys.argv[1], "t.prp"), "wb").write(out)
