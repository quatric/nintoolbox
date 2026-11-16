#!/usr/bin/env python3
"""Synthetic Blitz Games .rev package in the 2009 resource layout (iCarly, Wii; see
project/src/lib-rev.c): a CI8 texture with an RGB565 palette (format 25), a CI4 texture with
an RGB5A3 palette (format 28) and a static actor whose nodes carry the extra 16 bytes and
whose batch records are 0x18 bytes. usage: mk_rev2009.py OUTDIR  ->  OUTDIR/blitz09.rev"""
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


def be(*v):
    return struct.pack('>%dI' % len(v), *v)


def texture(name, w, h, fmt, pixels, palette):
    """0x60-byte header, pixels at 0x60, palette after them; 0x20-aligned total."""
    pal_off = 0x60 + len(pixels)
    end = pal_off + len(palette)
    size = (end + 0x1f) & ~0x1f
    t = bytearray(size)
    t[8:16] = be(crc(name), size)
    t[0x24:0x2a] = struct.pack('>HHH', w, h, fmt)
    t[0x2c] = 1  # mipmaps
    t[0x2d] = 1  # frames
    t[0x34:0x38] = be(w * h)
    t[0x3c:0x48] = be(pal_off, 0x60, end)
    t[0x60:pal_off] = pixels
    t[pal_off:end] = palette
    return bytes(t)


# 8x4 CI8 = one tile; pixel (0,0) uses entry 1 (RGB565 red), the rest entry 0 (blue)
ci8 = texture('t_ci8', 8, 4, 25, bytes([1] + [0] * 31),
              struct.pack('>2H', 0x001F, 0xF800) + bytes(508))
# 8x8 CI4 = one tile; pixel (0,0) uses entry 1 (opaque RGB5A3 red), the rest entry 0 (alpha 0)
ci4 = texture('t_ci4', 8, 8, 28, bytes([0x10] + [0] * 31),
              struct.pack('>2H', 0x0000, 0xFC00) + bytes(28))

# static actor: node at 0x100 (0x144 bytes), embedded mesh at node + 0x90, arrays after 0x300
act = bytearray(0x600)
act[4:8] = be(0x100)
act[8:16] = be(crc('tri_m'), len(act))
act[0xa0:0xa8] = be(0x100, 0x2004)
n = 0x100
act[n + 0x2c:n + 0x30] = struct.pack('>f', 1.0)
act[n + 0x50:n + 0x5c] = struct.pack('>3f', 1, 1, 1)
act[n + 0x80] = 2                     # type: static mesh (old +0x70)
act[n + 0x120:n + 0x124] = be(n)      # sibling ring: itself (old +0x110)
m = n + 0x90
ptr = dict(bat=0x400, prim=0x420, tab=0x440, dl=0x460, pos=0x500, nrm=0x540, uv=0x560, col=0x580)
act[m:m + 4] = be(3)
act[m + 8:m + 0x14] = be(1, ptr['bat'], ptr['prim'])
act[m + 0x4c:m + 0x50] = be(0x90)
act[m + 0x50:m + 0x6c] = be(ptr['pos'], ptr['nrm'], ptr['uv'], ptr['col'], ptr['dl'], 0, ptr['tab'])
# batch: u16 id, u16 0, u32 0, u32 texture list, u32 prim count, u32 texture CRC, u32 0
act[ptr['bat']:ptr['bat'] + 0x18] = struct.pack('>HH', 1, 0) + be(0, 0, 1, crc('t_ci8'), 0)
act[ptr['prim']:ptr['prim'] + 8] = bytes([5, 0, 0, 3, 0, 0, 0, 0])
act[ptr['tab']:ptr['tab'] + 8] = be(0, 0x20)
dl = b'\x00\x98\x00\x03' + b''.join(struct.pack('>4H', i, 0, 0, i) for i in range(3))
act[ptr['dl']:ptr['dl'] + len(dl)] = dl
for i, p in enumerate([(0, 0, 0), (1, 0, 0), (0, 1, 0)]):
    act[ptr['pos'] + 12 * i:ptr['pos'] + 12 * i + 12] = struct.pack('>3f', *p)
act[ptr['nrm']:ptr['nrm'] + 3] = bytes([0, 0, 64])
for i, uv in enumerate([(0, 0), (1, 0), (0, 1)]):
    act[ptr['uv'] + 8 * i:ptr['uv'] + 8 * i + 8] = struct.pack('>2f', *uv)

# container, align 0x20 (same as mk_rev.py)
files = [('t_ci8', ci8), ('t_ci4', ci4), ('tri_m', bytes(act))]
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
    blob += be(off, c, size, size, 1, 0, 0, 0)
hdr = be(0x12345678, align, 0, len(files), idx_off, 0, 0, 0, 0, 0, names_off, len(names), 32 * len(files))
blob[:len(hdr)] = hdr
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'blitz09.rev'), 'wb').write(blob)
