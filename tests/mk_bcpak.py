#!/usr/bin/env python3
"""Synthetic .bcpak resource pack: a TPL-magic entry and a raw entry. usage: mk_bcpak.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
items = [(b'\x00\x20\xaf\x30' + b'T' * 12, 0x11223344), (b'hello-bcpak!', 0xaabbccdd)]
off = (0xc + 16 * len(items) + 0xf) & ~0xf
hdr = struct.pack('>HHII', 0x9a3f, len(items), 0x12345678, 0xbcb0b530)[:0xc]
ents = b''; body = b''
for dt, h in items:
    ents += struct.pack('>IIII', len(dt), off + len(body), h, 0xc4326de2)
    body += dt.ljust((len(dt) + 15) & ~15, b'\0')
open(os.path.join(d, 't.bcpak'), 'wb').write((hdr + ents).ljust(off, b'\0') + body)
