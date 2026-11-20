"""Synthetic Sonic Colors text table (.xtb). Writes <dir>/t.xtb; "check <dir>" verifies t.xtb.d/t.json and t.txt."""
import json
import os
import struct
import sys


def pstr(s):
    return bytes([len(s)]) + s


def msg(i, style, text):
    u = text.encode("utf-16le")
    return pstr(i) + pstr(style) + struct.pack("<II", len(u), len(u) // 2) + u


if __name__ == "__main__":
    if sys.argv[1] == "check":
        d = os.path.join(sys.argv[2], "t.xtb.d")
        j = json.load(open(os.path.join(d, "t.json")))
        assert [s["name"] for s in j["styles"]] == ["s51", "Default"] and j["styles"][0]["color"] == "ffffff" and j["styles"][0]["size"] == 11, j
        assert [g["name"] for g in j["groups"]] == ["NAND", "WIFI"], j
        m = j["groups"][0]["messages"]
        assert m[0] == {"id": "ONE", "style": "s51", "text": "Hello\n\"w\" é"}, m
        assert j["groups"][1]["messages"][0]["text"] == "Bye", j
        t = open(os.path.join(d, "t.txt"), encoding="utf-8").read()
        assert t == "NAND.ONE\tHello\\n\"w\" é\nNAND.TWO\tx\nWIFI.THREE\tBye\n", repr(t)
        print("PASS")
        sys.exit(0)
    out = b"\xff" * 8 + struct.pack("<HH", 2, 2) + b"\0" * 6
    out += pstr(b"s51") + bytes([1, 11, 0, 0, 0, 255, 255, 255, 0, 0])
    out += pstr(b"Default") + bytes([1, 11, 0, 0, 0, 0, 0, 0, 0, 0])
    out += struct.pack("<II", 2, 0)
    out += pstr(b"NAND") + bytes([2]) + msg(b"ONE", b"s51", "Hello\n\"w\" é") + msg(b"TWO", b"Default", "x")
    out += pstr(b"WIFI") + bytes([1]) + msg(b"THREE", b"s51", "Bye")
    open(os.path.join(sys.argv[1], "t.xtb"), "wb").write(out)
