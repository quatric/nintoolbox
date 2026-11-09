#!/usr/bin/env python3
# Synthetic "OIBB" pak (see extract/oibb.inc): one 8x4 C8 TXTR object (0x40 header, 256-entry RGB5A3 palette, GX tile).
import struct, sys, os
out = sys.argv[1]
w, h = 8, 4
pal = b''.join(struct.pack('>H', 0x8000 | (i * 0x0421 & 0x7fff)) for i in range(256))
pix = bytes(range(32))
osz = 0x40 + len(pal) + len(pix)
hdr = b'TXTR' + struct.pack('>II', osz, len(pix)) + struct.pack('<I', 0x80) + struct.pack('>6I', w, h, 1, 9, 1, len(pal))
hdr = hdr.ljust(0x40, b'\0')
f = (b'OIBB' + struct.pack('<I', 4)).ljust(0x1c0, b'\0') + hdr + pal + pix
open(os.path.join(out, 't.pak'), 'wb').write(f)
