#!/usr/bin/env python3
"""Synthetic KP0 package (Power Punch .pkt): raw member, LZ10 member (literals only), RLE member, path reference. usage: mk_kp0.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
raw = b'hello world!'
lz = bytes([0x10]) + struct.pack('<I', len(raw))[:3]
for i in range(0, len(raw), 8):
    blk = raw[i:i + 8]; lz += b'\0' + blk            # flag byte 0 = 8 literals
rle = bytes([0x30]) + struct.pack('<I', 8)[:3] + bytes([0x82, 0x41, 0x02, 0x42, 0x43, 0x44])   # 5 x 'A'?? run(0x82)=5, then copy 3
names = [b'raw.bin', b'lz.bin', b'rle.bin', b'..\\ref\\x.dat']
datas = [(raw, 1, len(raw)), (lz, 0x1e01, len(raw)), (rle, 0x1e01, 8), (b'', 2, 0)]
nt = b''; offs = []
for n in names:
    offs.append(len(nt)); nt += n + b'\0'
body = b''; ents = b''
for i, (dt, fl, us) in enumerate(datas):
    ents += struct.pack('<6I', offs[i], len(dt), len(body), fl, us, len(names[i]) + 1)
    body += dt.ljust((len(dt) + 3) & ~3, b'\0')
st = (16 + len(ents) + len(nt) + 0x1f) & ~0x1f
open(os.path.join(d, 't.pkt'), 'wb').write((b'KP0\0' + struct.pack('<III', 4, st, 24) + ents + nt).ljust(st, b'\0') + body)
