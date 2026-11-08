#!/usr/bin/env python3
# Synthetic Slam Games audio bank t.atd: one 14-sample DSP-ADPCM sound named "snd".
import struct, sys, os
def ck(t, d):
    return t + struct.pack('<I', len(d)) + d
coefs = struct.pack('<16h', *range(1, 17))
blob = struct.pack('<I', 14) + coefs + struct.pack('<7H', 0, 0x10, 0, 0, 0, 0, 0) + bytes([0x10, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde])
audo = bytes([1, 3, 0, 3]) + struct.pack('<5I', 14, 22050, len(blob), 0, 0) + b'snd'
adat = struct.pack('<I', 4) + b'\0\0' + blob
d = b'SLAM AD\0' + struct.pack('<II', 0x30000, 4) + ck(b'DFTR', bytes(8)) + ck(b'AUDO', audo) + ck(b'ADAT', adat)
open(os.path.join(sys.argv[1], 't.atd'), 'wb').write(d)
# t.std: one IMAG chunk "img" holding a fake PGF blob
pgf = b'PGF\x06' + bytes(12)
imag = struct.pack('<HHIHHII', 4, 4, 0, 5, 7, 0, len(pgf)) + b'img\0' + pgf
std = b'SLAM TD\0' + struct.pack('<II', 0x50000, 4) + ck(b'DFTR', bytes(8)) + ck(b'IMAG', imag)
open(os.path.join(sys.argv[1], 't.std'), 'wb').write(std)
