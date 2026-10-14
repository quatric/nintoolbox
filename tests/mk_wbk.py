#!/usr/bin/env python3
# Synthetic Treyarch NSLB t.wbk (Kung Fu Panda layout): one mono DSP sound of 16 data bytes (28 samples).
import struct, sys, os
b = bytearray(0x400)
b[0:4] = b'NSLB'
struct.pack_into('<H', b, 4, 1)
struct.pack_into('<IIIIIIIII', b, 0x10, 1, 0, 0x100, 0x200, 0x40, 0x300, 0x20, 0x380, 0x10)
struct.pack_into('<I', b, 0x24, 0x300)       # info section
struct.pack_into('<I', b, 0x2c, 0x380)       # strings
struct.pack_into('<I', b, 0x34, 0x3c0)       # in-bank data
struct.pack_into('<I', b, 0x3c, 0x400)       # streamed data
struct.pack_into('<I', b, 0x100, 0)          # name offset
struct.pack_into('<IIII', b, 0x200, 0, 0, 16, 28)
struct.pack_into('<HHBBB', b, 0x300, 32000, 0, 0x22, 0, 1)
for j in range(8):
    struct.pack_into('<hh', b, 0x300 + 0x44 + 4 * j, 0x200 * (j + 1), 0x100 * (j + 1))
b[0x380:0x384] = b'snd\0'
b[0x3c0:0x3d0] = bytes(range(1, 17))
open(os.path.join(sys.argv[1], 't.wbk'), 'wb').write(b[:0x3d0])
