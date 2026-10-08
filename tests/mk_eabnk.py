#!/usr/bin/env python3
# Synthetic EA BNKb sound bank: slot 0 = GC DSP, slot 1 = EALayer3 (MPEG-2 mono), written as t.bnk.bin
# and wrapped in an ABKC container as t.abk.
import struct, sys, os
frames = 4
samples = frames * 14
coef = bytes(range(1, 33)) + b'\0'
def be(v, n): return v.to_bytes(n, 'big')
def tag(t, v, n): return bytes([t, n]) + be(v, n)
h0 = b'PT\x06\x00' + b'\xfd' + tag(0xa0, 0x12, 1) + tag(0x84, 22050, 2) + tag(0x85, samples, 1)
h0 += tag(0x88, 0x100, 2) + b'\x8f\x21' + coef + b'\xff'
h0 += b'\0' * (-len(h0) % 4)
h1 = b'PT\x06\x00' + b'\xfd' + tag(0xa0, 0x17, 1) + tag(0x82, 1, 1) + tag(0x84, 22050, 2) + tag(0x85, 576, 2)
h1 += tag(0x88, 0x140, 2) + b'\xff'
h1 += b'\0' * (-len(h1) % 4)
# MPEG-2 22.05 kHz mono EA frame, no payload: 0x00, hdr(ver 2, sr 0, mode 3, ext 0), gi=0, size 12b, 32b, 19b
bits = '00000000' + '10' + '00' + '11' + '00' + '0' + '0' * 12 + '0' * 32 + '0' * 19
bits += '0' * (-len(bits) % 8)
eaf = int(bits, 2).to_bytes(len(bits) // 8, 'big')
out = bytearray(b'BNKb\x05\x00' + be(2, 2) + struct.pack('>III', 0x200, 0x200, 0))
off0 = 0x14 + 8
off1 = off0 + len(h0)
out += struct.pack('>II', off0 - 0x14, off1 - 0x18)
out += h0 + h1
assert len(out) <= 0x100
out += b'\0' * (0x100 - len(out))
out += bytes(range(0x40, 0x40 + frames * 8))
out += b'\0' * (0x140 - len(out))
out += eaf * 3
d = sys.argv[1]
open(os.path.join(d, 't.bnk.bin'), 'wb').write(out)
abk = bytearray(0x40) + out
abk[0:4] = b'ABKC'
abk[0x20:0x24] = be(0x40, 4)
open(os.path.join(d, 't.abk'), 'wb').write(abk)
