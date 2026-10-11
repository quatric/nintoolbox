#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii attack table ("\0KTA", version 0x1021) as t.atk.
import struct, sys, os

d = sys.argv[1]
count = 2
f = bytearray(8 + count * 176)
f[1:4] = b'KTA'
struct.pack_into('>HH', f, 4, 0x1021, count)

# record 0
f[8:14] = b'NONAME'
struct.pack_into('>4B', f, 16, 0x7f, 0x7f, 0xff, 0xff)
struct.pack_into('>I', f, 20, 0x12345678)
struct.pack_into('>f', f, 24, 120.0)

# record 1
f[8 + 176 : 8 + 176 + 5] = b'SLASH'
struct.pack_into('>4B', f, 8 + 176 + 8, 0x00, 0x00, 0x00, 0x30)
struct.pack_into('>I', f, 8 + 176 + 12, 0x87654321)
struct.pack_into('>f', f, 8 + 176 + 16, 450.5)

open(os.path.join(d, 't.atk'), 'wb').write(bytes(f))
