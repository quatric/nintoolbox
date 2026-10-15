#!/usr/bin/env python3
"""Synthetic Konami Wii streaming bank t.vas (0x800 pages): two stereo streams, 0x100-byte channel interleave,
trailer page. usage: mk_konamivas.py OUTDIR"""
import os, struct, sys

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def stream(rate, fill0, fill1):
    data = bytes([fill0]) * 0x100 + bytes([fill1]) * 0x100       # interleaved: ch0 block, ch1 block
    hp = bytearray(0x800)
    struct.pack_into('>II', hp, 0, len(data), rate)
    for c in range(2):
        o = 0x80 + 0x40 * c
        struct.pack_into('>HHIII', hp, o, 0, 0, 2, 0x1ff, 2)
        for i in range(16):
            struct.pack_into('>h', hp, o + 0x10 + 2 * i, 0x100 * (i + 1))
    return bytes(hp) + data.ljust(0x800, b'\0')
body = stream(32000, 0x11, 0x21) + stream(44100, 0x31, 0x41)
pages = 1 + len(body) // 0x800 + 1
hdr = bytearray(0x800)
struct.pack_into('>III', hdr, 0, 0x5a8aab, pages - 1, 2)
open(os.path.join(d, 't.vas'), 'wb').write(bytes(hdr) + body + bytes(0x800))
