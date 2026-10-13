#!/usr/bin/env python3
# Synthetic Okami .t texture: one CI8 image, 8 wide x 4 high, RGB5A3 palette (index 1 = red)
import struct, sys, os
d = sys.argv[1]
hdr = struct.pack('>6I', 0x0020af30, 1, 0xc, 0x40, 0x30, 0)   # table at 0xc: {record 0x40, palette header 0x30}
phdr = struct.pack('>3I', 2 << 16, 2, 0x50)                   # 2 entries, RGB5A3, palette at 0x50
rec = struct.pack('>HHII', 4, 8, 9, 0x54)                     # height 4, width 8, CI8, pixels at 0x54
pal = struct.pack('>2H', 0x8000, 0xfc00)
pix = bytes([1] * 32)
img = hdr.ljust(0x30, b'\0') + phdr.ljust(0x10, b'\0')
img = img.ljust(0x40, b'\0')[:0x30] + phdr + bytes(4)
img = img.ljust(0x40, b'\0') + rec
img = img.ljust(0x50, b'\0') + pal + pix
open(os.path.join(d, 't.t'), 'wb').write(img)
