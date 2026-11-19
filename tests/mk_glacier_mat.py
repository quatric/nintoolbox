"""Synthetic Glacier ".MAT" (Mini Ninjas): header, two material names with TXID records. Writes <dir>/t.MAT; "check <dir>" verifies t.MAT.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.MAT.d", "t.json")))
        assert j["bindings"] == [{"material": "Characters/Ninja/Hat_01", "txid": 130}, {"material": "Effects/Fire_01", "txid": 131}], j
        print("PASS")
        sys.exit(0)
    body = b"Sprites\0Array\0\0\0"
    body += struct.pack(">HH", 0, 5) + b"Characters/Ninja/Hat_01\0" + b"NAME" + struct.pack(">IHH", 0x20, 8, 1) + b"TXID" + struct.pack(">IHH", 130, 1, 2)
    body += struct.pack(">HH", 0, 2) + b"Effects/Fire_01\0" + b"TXID" + struct.pack(">IHH", 131, 1, 2)
    hdr = struct.pack(">4I", 0x40, 0x40, 0, 0x100)
    out = hdr + body
    out += b"\0" * (0x40 - len(out)) if len(out) < 0x40 else b""
    open(os.path.join(sys.argv[1], "t.MAT"), "wb").write(out)
