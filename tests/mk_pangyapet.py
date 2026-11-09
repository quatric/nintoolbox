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
mdl = b''.join(struct.pack('>8I', 0, 0, par, 0, 0xffffffff, 0xffff0000, 0x400, 0) for par in (0xffff, 0))
trns = struct.pack('>16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) + struct.pack('>16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2.5, 0, 0, 1)
chunks = [(b'mdl', 2, 32, mdl), (b'trns', 2, 64, trns), (b'vbd', len(vbd), 1, vbd), (b'vbh', 1, 12, vbh), (b'ved', 4, 12, ved), (b'prm', 1, 0x20, bytes(prm))]
n = len(chunks)
off = 0x20 + 16 * n
body = b''; hdrs = b''
for tag, cnt, es, data in chunks:
    hdrs += tag.ljust(4, b'\0')[::-1] + struct.pack('>III', cnt, es, off + len(body))
    body += data.ljust((len(data) + 15) & ~15, b'\0')
g = struct.pack('>I', 0x20) + b'ffrg\0rmg' + struct.pack('>III', 0, 5, n) + bytes(8) + hdrs + body
mesh = b'MESH' + struct.pack('>IIIII', 0x1c + len(g), 0x10, 0x10000, 0, 0x1c)[:0x1c - 4] 
mesh = (b'MESH' + struct.pack('>I', 0x1c + len(g))).ljust(0x1c, b'\0') + g
names = [b'root\0', b'child\0']
bone = b'BONE' + bytes(4) + struct.pack('>IHH', 0x10, 2, 0)
no = 0x10 + 16
ent = b''
for i, nm in enumerate(names):
    ent += struct.pack('>IHH', no, i, len(nm)); no += len(nm)
bone += ent + b''.join(names)
bone = bone[:4] + struct.pack('>I', len(bone)) + bone[8:]
open(os.path.join(d, 't.pet'), 'wb').write(bone + mesh)

# skinned variant: no "ved", 33 byte vertices {u8 bone, float3 position (x + 10), float3 normal, float2 texcoord}
vbd2 = b''.join(b'\x01' + struct.pack('>8f', v[0] + 10, v[1], v[2], 0, 0, 1, v[6], v[7]) for v in verts)
vbh2 = struct.pack('>3I', 0x120401, len(vbd2), 0)
prm2 = bytearray(0x20); struct.pack_into('>HHHHHHHH', prm2, 4, 0, 0, 4, 33, 0x98, 2, 0xffff, 0)
chunks = [(b'vbd', len(vbd2), 1, vbd2), (b'vbh', 1, 12, vbh2), (b'prm', 1, 0x20, bytes(prm2))]
n = len(chunks)
off = 0x20 + 16 * n
body = b''; hdrs = b''
for tag, cnt, es, data in chunks:
    hdrs += tag.ljust(4, b'\0')[::-1] + struct.pack('>III', cnt, es, off + len(body))
    body += data.ljust((len(data) + 15) & ~15, b'\0')
g = struct.pack('>I', 0x20) + b'ffrg\0rmg' + struct.pack('>III', 0, 5, n) + bytes(8) + hdrs + body
open(os.path.join(d, 's.pet'), 'wb').write((b'MESH' + struct.pack('>I', 0x1c + len(g))).ljust(0x1c, b'\0') + g)
