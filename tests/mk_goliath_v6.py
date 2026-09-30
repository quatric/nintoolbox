#!/usr/bin/env python3
"""Synthetic old-dialect Goliath "GS" package inside the BABEB1B0 block-zlib
wrapper (The Amazing Spider-Man, Wii; see project/src/lib-goliath.c): 12-byte
chunk headers with no high id bit, the name record's hash tying each texture
header to its pixel payload in a 0x26 pool that is deliberately in the reverse
order of the headers. Two textures (a CMPR one and a 16x8 CMPR+I8 one).
usage: mk_goliath_v6.py OUTDIR -> OUTDIR/T6.pkz"""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def chunk(cid, version, body, children=False):
    return struct.pack('>IHHI', cid, version, 1 if children else 0, len(body)) + body


def name_record(name, h):
    body = bytearray(88)
    struct.pack_into('>II', body, 0, h, 4)
    nb = name.encode()
    body[0x18:0x18 + len(nb)] = nb
    return chunk(0x138e, 5, bytes(body))


def cmpr(w, h):
    return b''.join(struct.pack('>HHI', 0xf800 if i % 2 else 0x001f, 0, 0) for i in range(w * h // 16))


def tex(name, h, w, fmt, hash_):
    head = bytearray(0x2c)
    struct.pack_into('>IIII', head, 0, h, w, 1, fmt)
    colour = cmpr(w, h)
    pix = colour + (bytes((i * 5) & 0xff for i in range(w * h)) if fmt == 4 else b'')
    desc = chunk(0x138d, 2, name_record(name, hash_) + chunk(0x191, 6, chunk(0x197, 4, bytes(head)), True), True)
    pool = chunk(0x26, 1, name_record(name, hash_) + chunk(0x195, 2, pix), True)
    return desc, pool


d1, p1 = tex('SynthA_D[Mid]', 8, 16, 4, 0x11111111)
d2, p2 = tex('SynthB_D[Mid]', 8, 8, 3, 0x22222222)
body = chunk(0x11, 3, b'buildman - synthetic\0GS version 6.64.3029\0') \
    + chunk(0x9, 2, d1 + d2, True) + p2 + p1
pkg = chunk(1, 1, body, True)

# Wrapper: two 0x8000 blocks; the package is split in two zlib streams.
half = len(pkg) // 2
parts = [pkg[:half], pkg[half:]]
blocks = []
for p in parts:
    c = zlib.compressobj(9, zlib.DEFLATED, 15)
    z = c.compress(p) + c.flush(zlib.Z_SYNC_FLUSH)
    blocks.append(z.ljust(0x8000, b'\0'))
ends, acc = [], 0
for p in parts:
    acc += len(p)
    ends.append(acc)
head = struct.pack('>7I', 0xBABEB1B0, 0x8000, 0x8000, 0, len(parts), 0x8000 * 3, len(pkg))
head += b''.join(struct.pack('>I', e) for e in ends)
data = head.ljust(0x8000, b'\0') + b''.join(blocks)
with open(os.path.join(out, 'T6.pkz'), 'wb') as f:
    f.write(data)
