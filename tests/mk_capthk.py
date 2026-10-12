#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii AI decision trees ("\0KHT") as t.thk.
import struct, sys, os

d = sys.argv[1]
count = 2
nodes_0 = 3
nodes_1 = 2
off_0 = 16 + count * 16 # 16 + 32 = 48
off_1 = off_0 + nodes_0 * 176 # 48 + 528 = 576
sz = off_1 + nodes_1 * 176 + 32 # 576 + 352 + 32 = 960

f = bytearray(sz)
f[1:4] = b'KHT'
struct.pack_into('>III', f, 4, 0x14100000, count, 0)

# entry 0
struct.pack_into('>IIII', f, 16, (nodes_0 << 24), off_0, 0, 0)
# entry 1
struct.pack_into('>IIII', f, 32, (nodes_1 << 24), off_1, 0, 0)

open(os.path.join(d, 't.thk'), 'wb').write(bytes(f))
