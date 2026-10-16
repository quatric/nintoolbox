#!/usr/bin/env python3
# Synthetic stereo Krome t.mus: u32 samples, interleave 0x8000, start 0x80, data size, per channel DSP coefficient block
# at 0x10 (+0x2e), rate @0x6c, two 0x8000 byte blocks. Writes the expected decode of both channels (14 samples of each,
# little-endian s16, ch0 then ch1) to t.expect from an independent DSP-ADPCM decoder.
import struct, sys, os
d = sys.argv[1]
coef = [0x0800, 0x0000] + [0] * 14
frames = [bytes([0x01, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde]), bytes([0x02, 0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0xed])]
h = bytearray(0x80)
struct.pack_into('>IIII', h, 0, 14, 0x8000, 0x80, 0x10000)
for c in range(2):
    for i, v in enumerate(coef):
        struct.pack_into('>h', h, 0x10 + c * 0x2e + 2 * i, v)
struct.pack_into('>H', h, 0x6c, 32000)
blocks = b''
exp = []
for c in range(2):
    blk = frames[c] + bytes(0x8000 - 8)
    blocks += blk
    fr = frames[c]
    sc = 1 << (fr[0] & 15)
    c1, c2 = coef[2 * (fr[0] >> 4)], coef[2 * (fr[0] >> 4) + 1]
    h1 = h2 = 0
    out = []
    for i in range(14):
        n = (fr[1 + i // 2] >> (4 if i % 2 == 0 else 0)) & 15
        n = n - 16 if n >= 8 else n
        v = max(-32768, min(32767, (n * sc * 2048 + 1024 + c1 * h1 + c2 * h2) >> 11))
        out.append(v)
        h2, h1 = h1, v
    exp.append(out)
open(os.path.join(d, 't.mus'), 'wb').write(bytes(h) + blocks)
open(os.path.join(d, 't.expect'), 'wb').write(b''.join(struct.pack('<h', v) for ch in exp for v in ch))
