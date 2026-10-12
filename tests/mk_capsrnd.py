#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii surround sound parameter table ("SRND", version 2) as t.srnd.
import struct, sys, os

d = sys.argv[1]
count = 2
f = bytearray(16 + count * 140)
f[0:4] = b'SRND'
struct.pack_into('>III', f, 4, 2, count, 0)

# record 0
struct.pack_into('>II', f, 16, 0x1001, 0x00010002)
# record 1
struct.pack_into('>II', f, 16 + 140, 0x1002, 0x00020003)

open(os.path.join(d, 't.srnd'), 'wb').write(bytes(f))
