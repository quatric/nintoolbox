"""Synthetic Sonic Colors NGIF layout (.gncp). Writes <dir>/t.gncp; "check <dir>" verifies t.gncp.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.gncp.d", "t.json")))
        assert j["pointers"] == [12], j
        assert j["strings"] == [{"slot": 12, "offset": 16, "text": "Root"}], j
        assert j["words"][3] == 16, j
        print("PASS")
        sys.exit(0)
    data = b"nCPJ" + struct.pack("<I", 0x20) + struct.pack(">II", 0, 16) + b"Root\0\0\0\0" + b"\0" * 8
    nof_off = 0x20 + len(data)
    nof = b"NOF0" + struct.pack("<I", 0x18) + struct.pack(">II", 1, 0) + struct.pack(">I", 12) + b"\0" * 12
    hdr = b"NGIF" + struct.pack("<I", 0x18) + struct.pack(">6I", 1, 0x20, nof_off, nof_off, len(nof), 1)
    out = hdr + data + nof + b"NEND" + struct.pack("<I", 0x8) + b"\0" * 8
    open(os.path.join(sys.argv[1], "t.gncp"), "wb").write(out)
