#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii serialized object ("\0SFX", version 8) with one class of two members as t.xfs.
import struct, sys, os
d = sys.argv[1]
names = b'mAlpha\0mBeta\0'
f = bytearray(0x18 + 8 + 2 * 0x38 + len(names) + 0x10)
f[1:4] = b'SFX'
struct.pack_into('>HHII', f, 4, 8, 0, 1, 0x100)
struct.pack_into('>II', f, 0x10, 8, 0x40)
struct.pack_into('>II', f, 0x18, 0x1234abcd, 2 << 17)
str_base = 0x18 + 8 + 2 * 0x38
f[str_base:str_base + len(names)] = names
for i, (nm, ty, sz) in enumerate([(0, 0x06, 8), (7, 0x0c, 4)]):
    struct.pack_into('>IBBH', f, 0x20 + i * 0x38, str_base - 0x10 + nm, ty, 0, sz)
open(os.path.join(d, 't.xfs'), 'wb').write(bytes(f))
