"""Synthetic GLOB resource pack (Green Lantern: Rise of the Manhunters). Writes <dir>/t.glob and t.name.glob; "check <dir>" verifies t.glob.d."""
import os
import struct
import sys

A = b"alpha data\n" * 4
B = b"beta\n" * 5
NAMES = [b"Textures/Maps/a.bin", b"Models/b.bin"]
if __name__ == "__main__":
    d = sys.argv[2] if sys.argv[1] == "check" else sys.argv[1]
    if sys.argv[1] == "check":
        o = os.path.join(d, "t.glob.d")
        assert open(os.path.join(o, "Textures/Maps/a.bin"), "rb").read() == A
        assert open(os.path.join(o, "Models/b.bin"), "rb").read() == B
        print("PASS")
        sys.exit(0)
    n = 3
    tab = 8 + 12 * n
    data = A + B
    names = b"".join(x + b"\0" for x in NAMES)
    dat = b"GLOB" + struct.pack(">I", n)
    off = tab
    dat += struct.pack(">3I", 1, off, len(A)) + struct.pack(">3I", 4, off + len(A), len(B))
    dat += struct.pack(">3I", 0x7FFFFFFF, off + len(data), 0) + data
    nm = b"GLOB" + struct.pack(">I", n)
    o = tab
    for i, x in ((1, NAMES[0]), (4, NAMES[1])):
        nm += struct.pack(">3I", i, o, len(x) + 1)
        o += len(x) + 1
    nm += struct.pack(">3I", 0x7FFFFFFF, o, 0) + names
    open(os.path.join(d, "t.glob"), "wb").write(dat)
    open(os.path.join(d, "t.name.glob"), "wb").write(nm)
