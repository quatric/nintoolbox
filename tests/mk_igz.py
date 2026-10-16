#!/usr/bin/env python3
# Synthetic Alchemy IGZ t.igz: one geometry (a triangle strip of three vertices, pos f32x3 / color rgba8 / uv s16x2)
# textured with an 8x8 RGB565 igImage2 reached through an igTextureBindAttr2 -> igTextureAttr2 chain.
import struct, sys, os
d = sys.argv[1]
types = ['igGeometry', 'igGeometryAttr', 'igVertexBuffer', 'igVertexFormat', 'igIndexBuffer', 'igImage2',
         'igTextureAttr2', 'igTextureBindAttr2']
tsize = [40, 44, 32, 40, 36, 52, 56, 24]
P = {'geo': 4, 'ga': 48, 'vb': 96, 'vf': 128, 'bl': 168, 'ib': 232, 'bind': 272, 'ta': 296, 'img': 352}
sec1 = bytearray(420)
def w(o, v): struct.pack_into('>I', sec1, o, v)
def obj(name, t, nwords=0):
    w(P[name], t); w(P[name] + 4, 1)
for n, t in (('geo', 0), ('ga', 1), ('vb', 2), ('vf', 3), ('ib', 4), ('img', 5), ('ta', 6), ('bind', 7)):
    obj(n, t)
w(P['ga'] + 16, P['vb']); w(P['ga'] + 20, P['ib'])
w(P['vb'] + 8, 3); w(P['vb'] + 24, P['vf'])
w(P['vf'] + 8, 20); w(P['vf'] + 20, 0x80000040); w(P['vf'] + 24, P['bl'])
for i, (ident, f, ty) in enumerate(((9, 1, 4), (0xb, 1, 5), (0xd, 1, 3), (0xff, 0, 0))):
    for j, v in enumerate((ident, f, ty, 0)):
        w(P['bl'] + 16 * i + 4 * j, v)
w(P['ib'] + 8, 3); w(P['ib'] + 24, 0)
w(P['bind'] + 16, P['ta']); w(P['ta'] + 44, P['img'])
w(P['img'] + 12, (8 << 16) | 8); w(P['img'] + 16, 1); w(P['img'] + 24, 3)
order = ['geo', 'ga', 'vb', 'vf', 'ib', 'bind', 'ta', 'img']
objs = [P[n] for n in order]
memf = [P['vb'] + 20, P['ib'] + 20, P['img'] + 0x24]
def nib(vals):
    out = []
    for v in vals:
        while True:
            n = v & 7; v >>= 3
            out.append(n | (8 if v else 0))
            if not v: break
    if len(out) & 1: out.append(0)
    return bytes(out[i] | (out[i + 1] << 4) for i in range(0, len(out), 2))
def deltas(pos):
    prev = 0; r = []
    for p in pos:
        r.append((p - prev) // 4 - 1); prev = p
    return r
def rec(tag, count, payload):
    return struct.pack('>6I', tag, 0, 0, count, 24 + len(payload), 24) + payload
names = b''.join(t.encode() + b'\0' for t in types)
vdata = struct.pack('>I', 0x9f0003)
for p, c, uv in (((0, 0, 0), (255, 0, 0, 255), (0, 0)), ((1, 0, 0), (0, 255, 0, 255), (16384, 0)),
                 ((0, 1, 0), (0, 0, 255, 255), (0, 16384))):
    vdata += struct.pack('>3f', *p) + bytes(c) + struct.pack('>hh', *uv)
idata = struct.pack('>I', 0x9f0003) + bytes([0, 0, 0, 1, 1, 1, 2, 2, 2])
pix = bytearray()
for t in range(4):
    for i in range(16):
        pix += struct.pack('>H', [0xf800, 0x07e0, 0x001f, 0xffff][t])
blocks = [(len(vdata) + 3 & ~3, 2, 0x20), (32, 2, 0x20 + (len(vdata) + 31 & ~31)), (len(pix), 3, 0x20)]
recs = [rec(0, len(types), names + bytes(-len(names) % 4)),
        rec(13, len(tsize), struct.pack('>%dI' % len(tsize), *tsize)),
        rec(11, len(blocks), b''.join(struct.pack('>II', s, (p << 24) | o) for s, p, o in blocks)),
        rec(5, len(objs), nib(deltas(objs))), rec(12, len(memf), nib(deltas(memf)))]
fix = struct.pack('>5I', 0, 4, 0, 20, len(recs)) + b''.join(recs)
fix += bytes(-len(fix) % 4)
s0 = 0x100; s1 = s0 + len(fix) + 0x20; s2 = s1 + len(sec1) + 0x20; s3 = s2 + 2 + 0x20
p2len = 0x20 + (len(vdata) + 31 & ~31) + 0x20 + 0x20
head = b'IGZ\x01' + struct.pack('>I', 4) + bytes(8)
head += struct.pack('>4I', s0, len(fix), 0x800, 0) + struct.pack('>4I', s1, len(sec1), 0x20, 8)
head += struct.pack('>4I', s2, 2, 2, 15) + struct.pack('>4I', s3, p2len, 0x20, 22)
f = bytearray(s3 + p2len + 0x20 + len(pix))
f[:len(head)] = head
f[s0:s0 + len(fix)] = fix
f[s1:s1 + len(sec1)] = sec1
f[s3 + 0x20:s3 + 0x20 + len(vdata)] = vdata
o = s3 + 0x20 + (len(vdata) + 31 & ~31)
f[o:o + len(idata)] = idata
f[s3 + p2len + 0x20:] = pix
open(os.path.join(d, 't.igz'), 'wb').write(f)
