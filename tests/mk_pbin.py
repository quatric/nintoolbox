#!/usr/bin/env python3
# Synthetic Marvelous pBin: main body (DAT) + one TEX member holding an 8x8 RGB5A3 FIPAFTEX texture (red)
import struct, sys, os
d = sys.argv[1]
w = h = 8
pix = struct.pack('>H', 0xfc00) * (w * h)
tex_hdr = bytearray(0x40)
tex_hdr[0:8] = b'FIPAFTEX'
tex = bytes(tex_hdr)
ch = bytearray(0x40)
ch[0x14:0x18] = struct.pack('>HH', w, h)
ch[0x18:0x1c] = struct.pack('>I', 5)
ch[0x24:0x28] = struct.pack('>I', 1)
tex += bytes(ch) + pix
main = b'MAINBODY'
tab = 0x20
n = 1
main_off = 0x40
tex_off = main_off + 0x40
hdr = b'pBin' + struct.pack('>7I', 1, 1, 0xffffffff, tab, n, len(main), main_off)
rows = b'DAT\0' + struct.pack('>3I', 0, len(tex), tex_off) + b'TEX\0'
img = (hdr + rows).ljust(main_off, b'\0') + main.ljust(0x40, b'\0') + tex
open(os.path.join(d, 't.pBin'), 'wb').write(img)
