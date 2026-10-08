#!/usr/bin/env python3
# Synthetic EA "TXG " texture group with one 8x8 CMPR texture, written as t.txg.bin.
import struct, sys, os
rec = bytearray(88)
rec[0:4] = b'tex1'
struct.pack_into('>IHHI', rec, 0x10, 0, 4, 0xffff, 1)
struct.pack_into('>HHHH', rec, 0x40, 8, 8, 0xffff, 0)
rec[0x48] = 14
blk = struct.pack('>HH', 0xf800, 0x001f) + bytes([0x1b] * 4)
data = blk * 4
def chunk(tag, body):
    return tag + struct.pack('>I', len(body)) + body
out = b'TXG \x02\x02\x01\x00' + chunk(b'HEAD', b'\0' * 8) + chunk(b'TXHE', bytes(rec)) + chunk(b'CLHE', b'\0' * 0x18) + chunk(b'TXDA', data)
open(os.path.join(sys.argv[1], 't.txg.bin'), 'wb').write(out)
