#!/usr/bin/env python3
# Synthetic EA Tiburon TERF bundle t.dat holding one plain SHPG (8x4 C8 image + RGB5A3 palette) and one empty slot.
import struct, sys, os
w, h = 8, 4
pix = bytes(i % 4 for i in range(w * h))
img = bytes([0x19]) + (16 + len(pix)).to_bytes(3, 'big') + struct.pack('>HHHHHH', w, h, 0, 0, 0x2000, 0) + pix
pal = b''.join(struct.pack('>H', v) for v in (0x0000, 0xfc00, 0x83e0, 0x801f)) + b'\0' * (256 * 2 - 8)
palrec = bytes([0x32]) + (16 + len(pal)).to_bytes(3, 'big') + b'\0' * 12 + pal
shpg = b'SHPG' + b'\0\0\0\0' + struct.pack('>I', 1) + b'G359' + b'tst1' + struct.pack('>I', 0x20) + b'\0' * 8 + img + palrec
shpg = shpg[:4] + struct.pack('<I', len(shpg)) + shpg[8:]
pad = (len(shpg) + 0x3f) & ~0x3f
entries = struct.pack('>II', 0x40, len(shpg)) + bytes(8)
dir1 = b'DIR1' + struct.pack('>I', 8 + len(entries)) + entries
data = b'DATA' + struct.pack('>I', 0x40 + pad) + bytes(0x38)
body = (b'TERF' + struct.pack('>I', 0x40) + bytes(0x38)) + dir1 + data + shpg.ljust(pad, b'\0')
open(os.path.join(sys.argv[1], 't.dat'), 'wb').write(body)
