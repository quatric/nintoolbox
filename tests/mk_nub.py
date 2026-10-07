#!/usr/bin/env python3
# Synthetic Namco NUB sound bank with one mono IDSP stream (one DSP frame).
import struct, sys, os
out = sys.argv[1]
data = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77])
blk = bytearray(0x160)
blk[0:4] = b'IDSP'
struct.pack_into('>IIII', blk, 4, 0, 1, 32000, 14)
struct.pack_into('>I', blk, 0x2c, len(data))
dsp = bytearray(0x60)
struct.pack_into('>III', dsp, 0, 14, 16, 32000)
struct.pack_into('>III', dsp, 0x10, 2, 15, 2)
blk[0x40:0xa0] = dsp
struct.pack_into('>I', blk, 0xbc, len(data))
tbl_off = 0x1c
hdr_size = 0x100 + 0x160
hdr = bytearray(hdr_size)
struct.pack_into('>IIIIIII', hdr, 0, 0x20100, 0, 0xf, 1, hdr_size, len(data), 0x20)
struct.pack_into('>I', hdr, 0x1c, 0x100 - 0xbc)
hdr[0x100:0x100 + 0x160] = blk
open(os.path.join(out, 't.nub'), 'wb').write(bytes(hdr) + data)
