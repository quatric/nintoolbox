#!/usr/bin/env python3
# Synthetic Nippon Ichi IMY texture (Phantom Brave, Wii): 16x8 C8 (RGB565 palette, 2x2 GX 8x4 tiles) as t.imy and t.dat (the same texture embedded twice).
# Tile 0 red (literal unit + copy-left run), tile 1 green, tile 2 green (literal back-reference),
# tile 3 green (copy from above).
import struct, sys, os
d = sys.argv[1]
pal = struct.pack('>HHH', 0x0000, 0xf800, 0x07e0) + bytes(2 * 253)
ctl = bytes([0x00, 0xce,  0x00, 0xce,  0x10, 0xce,  0xdf])
lit = bytes([0x01, 0x01, 0x02, 0x02])
hdr = b'IMY\0' + struct.pack('<IHBBHH', 0x20 + 0x400 + 128, 16, 0x80, 8, 8, 256) + bytes(16)
imy = hdr + pal + struct.pack('<H', len(ctl)) + ctl + lit
open(os.path.join(d, 't.imy'), 'wb').write(imy)
# the same texture twice inside a game data file (carved at 4-byte boundaries, offsets 0x10 and 0x500)
dat = bytearray(0x10) + imy
dat += bytes(0x500 - len(dat)) + imy + bytes(0x20)
open(os.path.join(d, 't.dat'), 'wb').write(bytes(dat))
