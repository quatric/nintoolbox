"""Synthetic Igor ".mkr" voice-over cue markers. Writes <dir>/t.mkr; "check <dir>" verifies t.mkr.d/t.tsv."""
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.mkr.d", "t.tsv")).read()
        assert got == "0\tC_01\n1.5\tC_02\n", got
        print("PASS")
        sys.exit(0)
    out = struct.pack("<I", 2)
    for t, n in ((0.0, b"C_01"), (1.5, b"C_02")):
        out += struct.pack("<fI", t, len(n)) + n
    open(os.path.join(sys.argv[1], "t.mkr"), "wb").write(out)
