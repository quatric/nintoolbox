#!/usr/bin/env python3
"""Synthetic So Blonde "WB-L10n" string table (see project/src/wszst_cmd/extract/wbl10n.inc).
usage: mk_wbl10n.py OUTDIR -> OUTDIR/text.dat"""
import os, struct, sys

strs = [b'Hallo Welt', b'Zweiter Text']
first = 0x24 + 4 * len(strs)
offs, body = [], b''
for s in strs:
    offs.append(first + len(body))
    body += s + b'\0'
out = b'WB-L10n\0' + struct.pack('<I', 1) + b'de-DEText'.ljust(16, b'\0') + struct.pack('<2I', len(strs), 1)
out += b''.join(struct.pack('<I', o) for o in offs) + body
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'text.dat'), 'wb').write(out)
