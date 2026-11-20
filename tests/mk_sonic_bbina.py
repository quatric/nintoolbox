"""Synthetic Sonic Team BBINA file (Sonic Colors .orc). Writes <dir>/t.orc; "check <dir>" verifies t.orc.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.orc.d", "t.json")))
        assert j["pointers"] == [3, 5, 7], j
        assert [s["text"] for s in j["strings"]] == ["qua_yo_fish", "qua_yo_watersplash"], j
        assert j["words"][:3] == [4539974, 1, 2], j
        print("PASS")
        sys.exit(0)
    data = struct.pack(">8I", 0x454646, 1, 2, 0x10, 0, 0x20, 1, 0x2C) + b"qua_yo_fish\0" + b"qua_yo_watersplash\0\0"
    data += b"\0" * (-len(data) % 4)
    hdr = struct.pack(">5I", 0, len(data), 4, 0, 0) + struct.pack(">HH", 0, 0x3142) + b"BINA" + b"\0\0\0\0"
    # pointer slots 3, 5, 7: deltas 3, 2, 2 as 6-bit entries, then the end byte
    reloc = bytes([0x40 | 3, 0x40 | 2, 0x40 | 2, 0])
    reloc += b"\0" * (-len(reloc) % 4)
    out = bytearray(hdr + data + reloc)
    struct.pack_into(">I", out, 0, len(out))
    open(os.path.join(sys.argv[1], "t.orc"), "wb").write(bytes(out))
