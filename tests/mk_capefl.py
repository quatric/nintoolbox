#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii effect file ("\0LFE") as t.efl.
import struct, sys, os

d = sys.argv[1]
psz = 64
count = 1
f = bytearray(0x20 + psz)
f[1:4] = b'LFE'
struct.pack_into('>III', f, 4, 0x20090914, psz, count)

# chunk 0: type 0x10, offset 0x20 (rel to 0x20 -> 0x40)
struct.pack_into('>IIII', f, 0x20, 0x10000020, 0x01000000, 0x12345678, 0x87654321)

open(os.path.join(d, 't.efl'), 'wb').write(bytes(f))
