#!/usr/bin/env python3
"""Synthetic CRI USM: CRID header chunk, two video chunks + audio chunk, end chunk. usage: mk_usm.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def chunk(tag, typ, payload, ch=0, pad=0):
    body = payload + b'\0' * pad
    size = 0x18 + len(body)
    return tag + struct.pack('>IBBHB2sBIIQ', size, 0, 0x18, pad, ch, b'\0\0', typ, 0, 30000, 0) + body
u = chunk(b'CRID', 1, b'@UTF' + b'\0' * 12)
u += chunk(b'@SFV', 0, b'\x00\x00\x01\xb3AAAA', 0, 4)
u += chunk(b'@SFA', 0, b'\x80\x00\x00\x20BB', 0)
u += chunk(b'@SFV', 0, b'CCCC', 0, 2)
u += chunk(b'@SFV', 2, b'#BLOCK END', 0)
open(os.path.join(d, 't.usm'), 'wb').write(u)
