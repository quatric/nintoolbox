#!/usr/bin/env python3
"""Synthetic Blue Castle Games ".big" archive (see project/src/lib-bcbig.h):
a text member, a duplicate name, a nested archive and a stereo ".dspi".
usage: mk_bcbig.py OUTDIR -> OUTDIR/T.big"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def big(members):
    names = b''
    offs = []
    for n, _, _ in members:
        offs.append(len(names)); names += n.encode() + b'\0'
    table = 0x18
    nm = table + 20 * len(members)
    data = (nm + len(names) + 7) & ~7
    body = b''; ents = []
    for (n, p, kind), no in zip(members, offs):
        ents.append((nm + no, len(p), data + len(body), kind)); body += p + b'\0' * (-len(p) % 8)
    total = data + len(body)
    h = struct.pack('<6I', 0x01020304, data, total, len(members), table, nm)
    t = b''.join(struct.pack('<5I', *e, 0) for e in ents)
    return h + t + names + b'\0' * (data - nm - len(names)) + body

def dsp(nch, nib):
    per = ((nib + 1) // 2 + 7) & ~7
    hdr = struct.pack('>3I', nib * 14 // 16, nib, 32000) + b'\0' * 0x54
    return hdr * nch + b''.join(bytes([c + 1]) * per for c in range(nch))[:nch * per - (per - (nib + 1) // 2)]

inner = big([('x.txt', b'inner\n', 4)])
members = [('a.txt', b'hello\n' * 9, 32), ('A.TXT', b'dup\n', 32),
           ('nest.big', inner, 4), ('s.dspi', dsp(2, 100), 2048)]
open(os.path.join(out, 'T.big'), 'wb').write(big(members))
