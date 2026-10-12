#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii scripted resource table ("SCRA", version 7) as t.scra.
import struct, sys, os

d = sys.argv[1]
count = 2
f = bytearray(8 + count * 8)
f[0:4] = b'SCRA'
struct.pack_into('>HH', f, 4, 7, count)

# record 0
struct.pack_into('>II', f, 8, 0x12345678, 0x9abcdef0)
# record 1
struct.pack_into('>II', f, 16, 0x11223344, 0x55667788)

open(os.path.join(d, 't.scra'), 'wb').write(bytes(f))
