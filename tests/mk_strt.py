#!/usr/bin/env python3
"""Synthetic One Piece "STRT" string table (see project/src/wszst_cmd/extract/strt.inc).
usage: mk_strt.py OUTDIR -> OUTDIR/msg.en.bin"""
import os, struct, sys

strs = [b'Hello there.', b'Second\r\nline.']
first = 12 + 4 * len(strs)
offs, body = [], b''
for s in strs:
    offs.append(first + len(body))
    body += s + b'\0'
out = b'STRT' + struct.pack('>I', 0x28) + b'en\0\0' + b''.join(struct.pack('>I', o) for o in offs) + body
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'msg.en.bin'), 'wb').write(out)
