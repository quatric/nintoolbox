#!/usr/bin/env python3
"""Synthetic Pangya .pet: MESH chunk with a grff 'rmg' container (vbd/vbh/ved/prm), one strip primitive of 4 vertices (pos+normal+uv). usage: mk_pangyapet.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
verts = [(0, 0, 0, 0, 0, 1, 0, 0), (1, 0, 0, 0, 0, 1, 1, 0), (0, 1, 0, 0, 0, 1, 0, 1), (1, 1, 0, 0, 0, 1, 1, 1)]
vbd = b''.join(struct.pack('>8f', *v) for v in verts)            # stride 32: pos, normal, uv
ved = struct.pack('>3I', 0, 2, 0) + struct.pack('>3I', 12, 2, 0x30000) + struct.pack('>3I', 24, 1, 0x50000) + struct.pack('>3I', 0xff0000, 0xffffffff, 0)
vbh = struct.pack('>3I', 0x120401, len(vbd), 0)
prm = bytearray(0x20); struct.pack_into('>HHHHHHHH', prm, 4, 0, 0, 4, 32, 0x98, 2, 0xffff, 0)
chunks = [(b'vbd', len(vbd), 1, vbd), (b'vbh', 1, 12, vbh), (b'ved', 4, 12, ved), (b'prm', 1, 0x20, bytes(prm))]
n = len(chunks)
off = 0x20 + 16 * n
body = b''; hdrs = b''
for tag, cnt, es, data in chunks:
    hdrs += tag.ljust(4, b'\0')[::-1] + struct.pack('>III', cnt, es, off + len(body))
    body += data.ljust((len(data) + 15) & ~15, b'\0')
g = struct.pack('>I', 0x20) + b'ffrg\0rmg' + struct.pack('>III', 0, 5, n) + bytes(8) + hdrs + body
mesh = b'MESH' + struct.pack('>IIIII', 0x1c + len(g), 0x10, 0x10000, 0, 0x1c)[:0x1c - 4] 
mesh = (b'MESH' + struct.pack('>I', 0x1c + len(g))).ljust(0x1c, b'\0') + g
open(os.path.join(d, 't.pet'), 'wb').write(mesh)
