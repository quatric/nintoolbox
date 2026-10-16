#!/usr/bin/env python3
# Synthetic mono Alfa System t.sts (Castle of Shikigami III layout): 0x70 byte header, 2 DSP frames (28 samples) at 0x70.
# Writes the expected PCM from an independent DSP-ADPCM decoder to t.expect (little-endian s16).
import struct, sys, os
d = sys.argv[1]
coef = [0x0800, 0x0000, 0x0400, 0x0400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
coef[0:2] = [0x0800, 0x0000]
frames = bytes([0x01, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0x11, 0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96])
per = len(frames)
h = bytearray(0x70)
struct.pack_into('>I', h, 0, 0x70 + per - 4)
struct.pack_into('>H', h, 0x0c, 22050)
struct.pack_into('>I', h, 0x1a, per)
for i, c in enumerate(coef):
    struct.pack_into('>h', h, 0x1e + 2 * i, c)
open(os.path.join(d, 't.sts'), 'wb').write(bytes(h) + frames)
c1, c2 = coef[0], coef[1]
h1 = h2 = 0
out = []
for f in range(per // 8):
    fr = frames[8 * f:8 * f + 8]
    sc = 1 << (fr[0] & 15)
    ci = fr[0] >> 4
    c1, c2 = coef[2 * ci], coef[2 * ci + 1]
    for i in range(14):
        n = (fr[1 + i // 2] >> (4 if i % 2 == 0 else 0)) & 15
        n = n - 16 if n >= 8 else n
        v = (n * sc * 2048 + 1024 + c1 * h1 + c2 * h2) >> 11
        v = max(-32768, min(32767, v))
        out.append(v)
        h2, h1 = h1, v
open(os.path.join(d, 't.expect'), 'wb').write(b''.join(struct.pack('<h', v) for v in out))
