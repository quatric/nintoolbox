#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii collision table ("\0LOC", version 2) as t.col.
import struct, sys, os

d = sys.argv[1]
count = 2
# group 0 has 1 shape (64 bytes), group 1 has 1 shape (64 bytes)
# header: 8 bytes
# group 0 header: 16 bytes, sub 0: 64 bytes
# group 1 header: 16 bytes, sub 0: 64 bytes
# trailer: 8 bytes
sz = 8 + 16 + 64 + 16 + 64 + 8
f = bytearray(sz)
f[1:4] = b'LOC'
struct.pack_into('>HH', f, 4, 2, count)

# group 0
pos = 8
struct.pack_into('>IIII', f, pos, 0xffffffff, 0xffffffff, 1, 4)
pos += 16
struct.pack_into('>IIII', f, pos, 0xffffffff, 0xffffffff, 0, 0x12)
pos += 64

# group 1
struct.pack_into('>IIII', f, pos, 0xffffffff, 0x10, 1, 2)
pos += 16
struct.pack_into('>IIII', f, pos, 0xffffffff, 0xffffffff, 1, 0x34)
pos += 64

# trailer
struct.pack_into('>II', f, pos, 0xffffffff, 0x1000)

open(os.path.join(d, 't.col'), 'wb').write(bytes(f))
