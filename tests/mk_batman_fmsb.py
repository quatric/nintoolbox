"""Synthetic Batman .fmsb. Writes <dir>/t.fmsb; "check <dir>" verifies t.fmsb.d/t.json."""
import json
import os
import struct
import sys

if __name__ == "__main__":
    if sys.argv[1] == "check":
        j = json.load(open(os.path.join(sys.argv[2], "t.fmsb.d", "t.json")))
        o = j["objects"]
        assert len(o) == 2 and o[0]["model"] == "dc_barrel.brmdl" and o[0]["position"] == [1.5, 2, -3], o
        assert o[1]["model"] == "script_play_sound.brmdl" and o[1]["extra"][0]["string"] == "a.wav", o
        print("PASS")
        sys.exit(0)

    def rec(name, pos, extra=b""):
        r = bytearray(0xA0)
        struct.pack_into(">fII", r, 0, 0.5, 0, 0xFFFFFFFF)
        struct.pack_into(">3f3f", r, 0x2C, *pos, 1, 1, 1)
        r[0x44:0x44 + len(name)] = name
        return bytes(r) + extra

    r0 = rec(b"dc_barrel.brmdl", (1.5, 2, -3))
    r1 = rec(b"script_play_sound.brmdl", (0, 0, 0), b"a.wav\0\0\0")
    base = 0xE0 + 8
    out = bytearray(0xE0) + struct.pack(">II", base, base + len(r0))
    struct.pack_into(">II", out, 0x90, 2, 0xE0)
    open(os.path.join(sys.argv[1], "t.fmsb"), "wb").write(bytes(out) + r0 + r1)
