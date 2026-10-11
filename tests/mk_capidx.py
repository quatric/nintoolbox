#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii actor index table ("\0XDI") as t.idx.
import struct, sys, os

d = sys.argv[1]
count = 2
f = bytearray(8 + count * 24)
f[1:4] = b'XDI'
struct.pack_into('>I', f, 4, count)

struct.pack_into('>II', f, 8, 0x10, 0x20)
struct.pack_into('>5I', f, 16, 1, 2, 3, 4, 5)
struct.pack_into('>5I', f, 36, 6, 7, 8, 9, 10)

open(os.path.join(d, 't.idx'), 'wb').write(bytes(f))
