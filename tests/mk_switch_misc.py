#!/usr/bin/env python3
"""Synthetic AAL directivity (".baadr") and VoiceText database (".vtdb2") files.

OUTDIR gets:
  t.baadr  AADR version 1: inner angle 30, outer angle 90, outer volume 0.25, unknown 1
  t.vtdb2  three members (name size 0x40, key 10): "a/db_build.date" ("Jan 1 2000\\0"), "b/wab.txt2"
           (word list "alpha\\nbeta\\n", stored + 10) and "c/p.bin" (4 raw bytes)
"""
import os, struct, sys


def aadr():
    return b"AADR" + struct.pack("<HH4f", 0xFEFF, 1, 30.0, 90.0, 0.25, 1.0)


def vtdb2():
    nl, key = 0x40, 10
    shift = lambda b: bytes((c + key) & 0xFF for c in b)
    members = [
        (b"a/db_build.date", b"Jan 1 2000\0"),
        (b"b/wab.txt2", shift(b"alpha\nbeta\n")),
        (b"c/p.bin", b"\x01\x02\x03\x04"),
    ]
    info = b"3 12 3 0 0 test test-D32-FileIO".ljust(nl, b"\0")
    out = struct.pack("<3I", len(members), nl, key) + info
    data_off = len(out) + len(members) * (2 * nl + 8)
    body = b""
    for name, data in members:
        src = shift(b"/build/test.vtdb2").ljust(nl, bytes([key]))
        out += shift(name).ljust(nl, bytes([key])) + src + struct.pack("<2I", data_off + len(body), len(data))
        body += data
    return out + body


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "t.baadr"), "wb") as f:
        f.write(aadr())
    with open(os.path.join(outdir, "t.vtdb2"), "wb") as f:
        f.write(vtdb2())


if __name__ == "__main__":
    main(sys.argv[1])
