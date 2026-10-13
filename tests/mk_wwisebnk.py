#!/usr/bin/env python3
# Synthetic Wwise bank (Epic Mickey, Wii): BKHD + DIDX + DATA with one mono RIFX / DSP media (id 4242,
# 16000 Hz, coefficients 1..16, one ADPCM frame) and one non-DSP media (id 7, kept as .wem).
import struct, sys, os
d = sys.argv[1]
def chunk(tag, body):
    return tag + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')
fmt = struct.pack('>HHIIHHHH', 0xfff0, 1, 16000, 0x9c7a, 4, 16, 0x16, 0x10) + bytes(12)
wiih = struct.pack('>16h', *range(1, 17)) + struct.pack('>HH', 0, 0x10) + bytes(12)
frame = bytes([0x10, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde])
riff = b'WAVE' + chunk(b'fmt ', fmt) + chunk(b'WiiH', wiih) + chunk(b'data', frame)
m0 = b'RIFX' + struct.pack('>I', len(riff)) + riff
m1 = b'RIFX' + struct.pack('>I', 4) + b'WAVE'
m0 += bytes(-len(m0) % 16)
didx = struct.pack('>III', 4242, 0, len(m0)) + struct.pack('>III', 7, len(m0), len(m1))
bank = chunk(b'BKHD', struct.pack('>II', 0x2e, 1) + bytes(20)) + chunk(b'DIDX', didx) + chunk(b'DATA', m0 + m1)
open(os.path.join(d, 't.bnk'), 'wb').write(bank)
