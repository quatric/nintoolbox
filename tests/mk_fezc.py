#!/usr/bin/env python3
"""Synthetic "FEZC" package (see project/src/lib-fezc.h): one sync-flushed zlib
member (flags 0x102), one stored member (flags 0) and a nested path.
usage: mk_fezc.py OUTDIR -> OUTDIR/T.fez"""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def sync_flush(data):
    # zlib stream ending in a sync flush (no final block), as the retail files do
    c = zlib.compressobj(9, zlib.DEFLATED, 15)
    body = c.compress(data) + c.flush(zlib.Z_SYNC_FLUSH)
    return body

members = [
    ('data/strings.gvf', b'FEZ-STRINGS\n' * 30, True),
    ('data/music/raw.bin', bytes(range(120)), False),
]
N = len(members)
names_off = 0x28
names = b''
for n, _, _ in members:
    padded = (len(n) + 1 + 3) & ~3
    names += struct.pack('<I', padded) + n.encode().ljust(padded, b'\0')
records_off = names_off + len(names)
data_off = records_off + 32 * N
recs = b''
blob = b''
for n, payload, zl in members:
    stream = sync_flush(payload) if zl else payload
    off = data_off + len(blob)
    recs += struct.pack('<IIII', 0x102 if zl else 0, off, len(stream), len(payload)) + b'\0' * 16
    blob += stream
hdr = b'FEZC' + struct.pack('<IIIII', 0x10000, N, names_off, records_off, data_off)
hdr = hdr.ljust(names_off, b'\0')
open(os.path.join(out, 'T.fez'), 'wb').write(hdr + names + recs + blob)
