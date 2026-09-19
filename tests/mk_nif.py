#!/usr/bin/env python3
"""Synthetic Gamebryo 20.6.0.0 Wii file (see project/src/lib-nif.h): a root
NiNode holding one textured NiMesh (one INDEX-list triangle) plus a 4x4 RGBA8
NiPersistentSrcTextureRendererData whose pixel (0,0) is opaque red.
usage: mk_nif.py OUTDIR -> OUTDIR/tri.nif"""
import os, struct, sys

B = lambda *v: struct.pack('>%dI' % len(v), *v)
F = lambda *v: struct.pack('>%df' % len(v), *v)
NONE = 0xFFFFFFFF
strings = ['Scene Root', 'tri', 'INDEX', 'POSITION', 'TEXCOORD', 'red.dds']
S = {s: i for i, s in enumerate(strings)}
ident = F(1, 0, 0, 0, 1, 0, 0, 0, 1)

def av(name, props=(), children=None):
    b = B(S[name], 0, NONE) + struct.pack('>H', 0) + F(0, 0, 0) + ident + F(1.0)
    b += B(len(props), *props) + B(NONE)
    if children is not None:
        b += B(len(children), *children) + B(0)
    return b

# block order: 0 root NiNode, 1 NiMesh, 2 NiTexturingProperty, 3 NiSourceTexture,
# 4 NiPersistentSrcTextureRendererData, 5 INDEX stream, 6 vertex stream
root = av('Scene Root', children=[1])
mesh = av('tri', props=[2])
mesh += B(0) + B(NONE) + b'\0' + B(0) + struct.pack('>HB', 1, 0) + F(0, 0, 0, 1)
mesh += B(2)
mesh += B(5) + b'\0' + struct.pack('>HH', 1, 0) + B(1, S['INDEX'], 0)
mesh += B(6) + b'\0' + struct.pack('>HH', 1, 0) + B(2, S['POSITION'], 0, S['TEXCOORD'], 0)
mesh += B(0)
texprop = B(NONE, 0, NONE) + struct.pack('>H', 0) + B(9) + b'\1' + B(3) + b'\0' * 12
srctex = B(NONE, 0, NONE) + b'\0' + B(S['red.dds'], 4) + B(0) * 4 + b'\0' * 3
pix = bytearray(64)
pix[0:2] = b'\xff\xff'          # A,R of pixel 0
pix[32:34] = b'\x00\x00'        # G,B
pdata = B(1) + b'\0' * 55 + B(NONE, 1, 4) + B(4, 4, 0) + B(64, 64, 1, 4) + bytes(pix)
assert len(pdata) == 0x47 + 12 + 16 + 64
idx = B(6, 0, 1) + B(0, 3) + B(1) + B(0x10215) + struct.pack('>3H', 0, 1, 2) + b'\0'
vs = b''.join(F(*p) + F(*t) for p, t in [((0, 0, 0), (0, 0)), ((1, 0, 0), (1, 0)), ((0, 0, 1), (0, 1))])
vstream = B(len(vs), 0, 1) + B(0, 3) + B(2) + B(0x30437, 0x20436) + vs + b'\0'
blocks = [('NiNode', root), ('NiMesh', mesh), ('NiTexturingProperty', texprop), ('NiSourceTexture', srctex),
          ('NiPersistentSrcTextureRendererData', pdata), ('NiDataStream', idx), ('NiDataStream', vstream)]
types = sorted({t for t, _ in blocks})
out = b'Gamebryo File Format, Version 20.6.0.0\n' + struct.pack('<IBII', 0x14060000, 0, 0, len(blocks))
out += struct.pack('>H', len(types))
for t in types:
    out += B(len(t)) + t.encode()
out += b''.join(struct.pack('>H', types.index(t)) for t, _ in blocks)
out += B(*[len(d) for _, d in blocks])
out += B(len(strings), max(map(len, strings)))
for s in strings:
    out += B(len(s)) + s.encode()
out += B(0)
out += b''.join(d for _, d in blocks) + B(1, 0)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'tri.nif'), 'wb').write(out)
