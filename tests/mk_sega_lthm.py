#!/usr/bin/env python3
"""Synthetic SEGA LTHM chunk stream (Sakura Wars bromide viewer .bf1; see project/src/wszst_cmd/extract/sega_res.inc): a 0x10-byte header
followed by chunks {tag, payload size, header size 0x10, 0}; here an HSPR and an ABDA stub (the split must not need valid payloads).
usage: mk_sega_lthm.py OUT.bf1"""
import struct, sys
def chunk(tag, payload):
    return tag + struct.pack('<III', len(payload), 0x10, 0) + payload
body = chunk(b'HSPR', bytes(0x20)) + chunk(b'ABDA', bytes(0x30))
hdr = b'LTHM' + struct.pack('<III', len(body), 0x10, 0)
open(sys.argv[1], 'wb').write(hdr + body)
