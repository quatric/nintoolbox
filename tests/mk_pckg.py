#!/usr/bin/env python3
"""Synthetic Town Factory PCKG: a plain member and a nested PCKG."""
import struct, sys, os

def pckg(members):
    out = b"PCKG" + b"\0" * 28
    for i, (name, data) in enumerate(members):
        body = data + b"\0" * (-len(data) % 32)
        last = i == len(members) - 1
        total = 0x20 + len(body)
        nxt = 0 if last else total
        sz = len(data)
        out += struct.pack(">III", nxt, sz, 0x20) + name.encode().ljust(20, b"\0") + body
    return out

inner = pckg([("in.txt", b"inner")])
open(os.path.join(sys.argv[1], "T.pac"), "wb").write(pckg([("a.txt", b"hello"), ("n.pcha", inner)]))
