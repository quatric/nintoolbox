#!/usr/bin/env python3
# Synthetic EA SHPG texture archive (one 8x4 C8 image + RGB5A3 palette), RefPack-wrapped literal-only, as t.gsh.
import struct, sys, os
w, h = 8, 4
pix = bytes(i % 4 for i in range(w * h))
img = bytes([0x19]) + (16 + len(pix)).to_bytes(3, 'big') + struct.pack('>HHHHHH', w, h, 0, 0, 0x2000, 0) + pix
pal = b''.join(struct.pack('>H', v) for v in (0x0000, 0xfc00, 0x83e0, 0x801f)) + b'\0' * (256 * 2 - 8)
palrec = bytes([0x32]) + (16 + len(pal)).to_bytes(3, 'big') + struct.pack('>IIII', 0x01000001, 0x01000000, 0, 0)[:12] + pal
palrec = bytes([0x32]) + (16 + len(pal)).to_bytes(3, 'big') + b'\0' * 12 + pal
body_off = 0x20
data = b'SHPG' + b'\0\0\0\0' + struct.pack('>I', 1) + b'G359' + b'tst1' + struct.pack('>I', body_off) + b'\0' * 8 + img + palrec
data = data[:4] + struct.pack('<I', len(data)) + data[8:]
out = bytearray(b'\x10\xfb' + len(data).to_bytes(3, 'big'))
p = 0
while len(data) - p >= 4:
    n = min(112, (len(data) - p) // 4 * 4)
    out.append(0xe0 | ((n - 4) >> 2))
    out += data[p:p + n]
    p += n
rest = data[p:]
out.append(0xfc | len(rest))
out += rest
open(os.path.join(sys.argv[1], 't.gsh'), 'wb').write(out)
