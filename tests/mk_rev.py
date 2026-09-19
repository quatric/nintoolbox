#!/usr/bin/env python3
"""Synthetic Blitz Games .rev package (see project/src/lib-rev.h): one RGBA8
texture and one static actor (a single textured triangle strip). usage:
mk_rev.py OUTDIR  ->  OUTDIR/blitz.rev"""
import os, struct, sys

def crc(name):
    t = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
        t.append(c)
    c = 0
    for b in name.lower().encode():
        c = ((c << 8) & 0xFFFFFFFF) ^ t[(c >> 24) ^ b]
    return c

def be(*v): return struct.pack('>%dI' % len(v), *v)

# 4x4 RGBA8 texture, one GX tile: AR plane then GB plane; pixel (0,0) opaque red
tex = bytearray(0xa0)
tex[0x20:0x2c] = be(4, 4, 15)
tex[0x6c:0x74] = be(0, 0xa0)
ar = bytearray(32); gb = bytearray(32)
for i in range(16):
    ar[i*2] = 255; ar[i*2+1] = 255 if i == 0 else 0
    gb[i*2] = 0; gb[i*2+1] = 0
tex += ar + gb

# static actor: node at 0x100, embedded mesh at 0x180, arrays after 0x300
act = bytearray(0x600)
act[4:8] = be(0x100)
act[0xa0:0xa8] = be(0x100, 0x2004)
n = 0x100
act[n+0x2c:n+0x30] = struct.pack('>f', 1.0)
act[n+0x50:n+0x5c] = struct.pack('>3f', 1, 1, 1)
act[n+0x70] = 2
act[n+0x110:n+0x114] = be(n)          # sibling ring: itself
m = n + 0x80
tex_crc = crc('tex1')
ptr = dict(bat=0x400, prim=0x420, tab=0x440, dl=0x460, pos=0x500, nrm=0x540, uv=0x560, col=0x580)
act[m:m+4] = be(3)                    # vertex count
act[m+8:m+0x14] = be(1, ptr['bat'], ptr['prim'])
act[m+0x4c:m+0x50] = be(0x90)
act[m+0x50:m+0x6c] = be(ptr['pos'], ptr['nrm'], ptr['uv'], ptr['col'], ptr['dl'], 0, ptr['tab'])
act[ptr['bat']:ptr['bat']+16] = be(1, tex_crc, 0, 0)
act[ptr['prim']:ptr['prim']+8] = bytes([5, 0, 0, 3, 0, 0, 0, 0])
act[ptr['tab']:ptr['tab']+8] = be(0, 0x20)
dl = b'\x00\x98\x00\x03' + b''.join(struct.pack('>4H', i, 0, 0, i) for i in range(3))
act[ptr['dl']:ptr['dl']+len(dl)] = dl
for i, p in enumerate([(0, 0, 0), (1, 0, 0), (0, 1, 0)]):
    act[ptr['pos']+12*i:ptr['pos']+12*i+12] = struct.pack('>3f', *p)
act[ptr['nrm']:ptr['nrm']+3] = bytes([0, 0, 64])
for i, uv in enumerate([(0, 0), (1, 0), (0, 1)]):
    act[ptr['uv']+8*i:ptr['uv']+8*i+8] = struct.pack('>2f', *uv)
act[ptr['col']:ptr['col']+4] = b'\x7f\x7f\x7f\x7f'

# container, align 0x20
files = [('tex1', bytes(tex)), ('tri_m', bytes(act)), ('FilenameTable.pak.sys', b'x')]
align = 0x20
names = b''.join(nm.encode() + b'\0' for nm, _ in files)
blob = bytearray(0x800)
ents = []
for nm, data in files:
    off = len(blob) // align
    blob += data + bytes(-len(data) % align)
    ents.append((crc(nm), off, len(data)))
names_off = len(blob) // align
blob += names + bytes(-len(names) % align)
idx_off = len(blob) // align
for c, off, size in sorted(ents):
    blob += be(off, c, size, size, 1, 4, 0, 0)
hdr = be(0x12345678, align, 0, len(files), idx_off, 0, 0, 0, 0, 0, names_off, len(names), 0)
blob[:len(hdr)] = hdr
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'blitz.rev'), 'wb').write(blob)
