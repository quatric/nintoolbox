"""Synthetic Glacier ".gms" (Mini Ninjas): {u32 LE size, u32 LE file size, 0, raw deflate}. Writes <dir>/t.gms; "check <dir>" verifies t.gms.d/t.bin."""
import os
import struct
import sys
import zlib

PAYLOAD = bytes(range(256)) * 8 + b"scene"

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.gms.d", "t.bin"), "rb").read()
        assert got == PAYLOAD
        print("PASS")
        sys.exit(0)
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    z = c.compress(PAYLOAD) + c.flush()
    out = struct.pack("<II", len(PAYLOAD), 9 + len(z)) + b"\0" + z
    open(os.path.join(sys.argv[1], "t.gms"), "wb").write(out)
