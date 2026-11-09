#!/usr/bin/env python3
"""Synthetic Octomania secB: header + two plain NW4R members (RSTM at 0x800, bres at 0x1800) + a packed block that must be skipped. usage: mk_octosec.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def member(magic, n):
    return magic + struct.pack('>HHI', 0xfeff, 0x100, n) + bytes(range(n - 12))
hdr = (b'secB' + struct.pack('>IIII', 1, 0x800, 0x40, 0x800)).ljust(0x800, b'\0')
a = member(b'RSTM', 0x40).ljust(0x1000, b'\0')
junk = bytes([0x14]) + b'RFNT' + bytes(15) + bytes(0x7e0)          # a packed-looking block: ignored
b = member(b'bres', 0x30).ljust(0x800, b'\0')
open(os.path.join(d, 't.sec'), 'wb').write(hdr + a + junk[:0x800].ljust(0x800, b'\0') + b)
