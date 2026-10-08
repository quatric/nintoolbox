#!/usr/bin/env python3
# Synthetic EA BNKb sound bank (GC DSP, one used slot + one empty), written as t.bnk.bin.
import struct, sys, os
frames = 4
samples = frames * 14
coef = bytes(range(1, 33)) + b'\0'
def be(v, n): return v.to_bytes(n, 'big')
hdr = b'PT\x06\x00' + b'\xfd' + b'\xa0\x01\x12' + b'\x84\x02' + be(22050, 2) + b'\x85\x01' + be(samples, 1)
hdr += b'\x88\x02' + be(0x100, 2) + b'\x8f\x21' + coef + b'\xff'
hdr += b'\0' * (-len(hdr) % 4)
table = 0x14
out = bytearray(b'BNKb\x05\x00' + be(2, 2) + struct.pack('>III', 0x200, 0x200, 0))
out += struct.pack('>II', 8, 0)
out += hdr
out += b'\0' * (0x100 - len(out))
out += bytes(range(0x40, 0x40 + frames * 8))
open(os.path.join(sys.argv[1], 't.bnk.bin'), 'wb').write(out)
