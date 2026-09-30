#!/usr/bin/env python3
"""Synthetic Harmonix Ark (see project/src/lib-rbark.h): a version 5 encrypted
main.hdr plus two data parts, with a nested dir, a "../" path and a member
that straddles nothing. usage: mk_rbark.py OUTDIR -> OUTDIR/T.hdr, T_0.ark, T_1.ark"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


def step(k):
    k = s32(k)
    q = int(k / 0x1F31D)
    v = s32(s32(k - q * 0x1F31D) * 0x41A7 - q * 0xB14)
    return v + 0x7FFFFFFF if v <= 0 else v


files = [('songs/a/x.bin', b'XX' * 50, 0), ('songs/a/y.dtb', bytes(range(200)), 0),
         ('../../system/z.txt', b'zed\n' * 20, 1), ('top.bin', b'top', 1)]
part = [b'', b'']
ents = []
glob_off = [0, 0]
sizes = [len(files[0][1]) + len(files[1][1]), len(files[2][1]) + len(files[3][1])]
for path, data, pi in files:
    ents.append((sum(sizes[:pi]) + len(part[pi]), path, len(data)))
    part[pi] += data
assert [len(p) for p in part] == sizes

blob = b''
idx = []
def sid(s):
    global blob
    o = len(blob); blob += s.encode() + b'\0'; idx.append(o); return len(idx) - 1
rows = b''
for off, path, size in ents:
    d, _, f = path.rpartition('/')
    rows += struct.pack('<qiiII', off, sid(f), sid(d), size, 0)

hdr = struct.pack('<III', 5, 2, 2) + struct.pack('<2I', *sizes)
hdr += struct.pack('<I', 2) + b''.join(struct.pack('<I', 5) + b'x.ark' for _ in range(2))
hdr += struct.pack('<I', len(blob)) + blob
hdr += struct.pack('<I', len(idx)) + b''.join(struct.pack('<i', i) for i in idx)
hdr += struct.pack('<I', len(ents)) + rows

key = 0x1234567
enc = bytearray()
k = key
for b in hdr:
    k = step(k); enc.append((b ^ k) & 0xff)
open(os.path.join(out, 'T.hdr'), 'wb').write(struct.pack('<i', key) + bytes(enc))
for i in range(2):
    open(os.path.join(out, 'T_%d.ark' % i), 'wb').write(part[i])
