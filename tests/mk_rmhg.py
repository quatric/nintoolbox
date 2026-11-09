#!/usr/bin/env python3
"""Synthetic Grasshopper RMHG container (.rsl): group {nested model container {GMF2}, texture container {GCT0}}. usage: mk_rmhg.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def cont(kind, items):          # items: (bytes, id, nested)
    first = 0x20 + 32 * len(items)
    ents = b''; body = b''
    for dt, i, nested in items:
        ents += struct.pack('<5I', first + len(body), len(dt), 1 if nested else 0, 0x427, i) + bytes(12)
        body += dt.ljust((len(dt) + 15) & ~15, b'\0')
    total = first + len(body)
    return (b'RMHG' + struct.pack('<IIIII', kind, 0x20, 0x427, total, 0) + bytes(8)) + ents + body
gm = cont(2, [(b'GMF2' + b'\x11' * 28, 7, False)])
tx = cont(1, [(b'GCT0' + b'\x22' * 28, 9, False)])
open(os.path.join(d, 't.rsl'), 'wb').write(cont(3, [(gm, 0, True), (tx, 3, True)]))
