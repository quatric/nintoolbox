#!/usr/bin/env python3
"""Synthetic Artefacts Studio ".map" (see project/src/lib-dbkmap.h): the
FAAFFAAF stream holding one texture object (name section, 8-byte record,
CMPR mip chain leaf).
usage: mk_dbkmap.py OUTDIR -> OUTDIR/T.map"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def sec(version, payload):
    """BBBBBBBB, end offset (patched by caller), version, payload, BEBEBEBE"""
    return [version, payload]

def emit(node, base):
    """Serialize a section tree at absolute offset `base`; returns bytes."""
    version, parts = node
    body = b''
    pos = base + 12
    for p in parts:
        if isinstance(p, bytes):
            body += p; pos += len(p)
        else:
            pad = -pos % 4
            body += b'\0' * pad; pos += pad
            b = emit(p, pos); body += b; pos += len(b)
    pad = -pos % 4
    body += b'\0' * pad; pos += pad
    end = pos
    return struct.pack('>III', 0xBBBBBBBB, end, version) + body + struct.pack('>I', 0xBEBEBEBE)

w, h, mips = 16, 16, 2
levels = [(16, 16), (8, 8)]
cmpr = b''.join(bytes([0xf8, 0x00, 0x07, 0xe0, 0xe4, 0xe4, 0xe4, 0xe4]) * 4 * ((lw + 7) // 8) * ((lh + 7) // 8)
                for lw, lh in levels)
name = struct.pack('>I', 20) + b'tex_test'.ljust(20, b'\0')
leaf = b'\x01' + struct.pack('>III', mips, len(cmpr), 0) + cmpr + b'\0\0\0'
tex = (3, [(2, [(2, [(1, [name, b'\0' * 4]), b'\0' * 4]), bytes([1, 0, 1]) + struct.pack('>HH', w, h) + b'\0']),
           b'\x01\xff\x00\x00', (1, [leaf])])
root = (1, [(2, [tex])])
body = emit(root, 8)
open(os.path.join(out, 'T.map'), 'wb').write(struct.pack('>II', 0xFAAFFAAF, 8) + body + struct.pack('>I', 0xFEEFFEEF))
