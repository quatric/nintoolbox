#!/usr/bin/env python3
"""Synthetic Neversoft Guitar Hero (Wii) T.pak.ngc and T.img.ngc."""
import struct, sys, os
out = sys.argv[1]
# pak: one named entry, one nameless entry, zero terminator, then payloads
hdr_len = 0x20 + 160 + 0x20 + 0x20
p1, p2 = b"hello", b"world!!"
e1 = struct.pack(">8I", 0xa7f505c4, hdr_len, len(p1), 1, 0, 2, 0, 0x20) + b"songs\\t.qb.ngc".ljust(160, b"\0")
e2 = struct.pack(">8I", 0x2cb3ef3b, hdr_len + len(p1), len(p2), 3, 0, 4, 0, 0)
open(os.path.join(out, "T.pak.ngc"), "wb").write(e1 + e2 + b"\0" * 32 + p1 + p2)
# img: 16x8 CMPR = 2x1 tiles of 32 bytes
body = bytes(range(64))
h = bytearray(0x20)
h[0:2] = b"\x04\x20"
h[10], h[11], h[13] = 4, 3, 14
struct.pack_into(">II", h, 0x10, len(body), 0x20)
open(os.path.join(out, "T.img.ngc"), "wb").write(bytes(h) + body)
