#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii effect link table ("\0OCE") as t.eco.
import struct, sys, os

d = sys.argv[1]
name = b'effect/fire\0'
count = 2
toff = 0x14 + count * 8
f = bytearray(toff + len(name))
f[1:4] = b'OCE'
struct.pack_into('>HH', f, 4, 1, count)
struct.pack_into('>I', f, 0x10, toff)

# entry 0: id 1, offset 0
struct.pack_into('>II', f, 0x14, 1, 0)
# entry 1: id 2, unused
struct.pack_into('>II', f, 0x1c, 2, 0xffffffff)

f[toff : toff + len(name)] = name

open(os.path.join(d, 't.eco'), 'wb').write(bytes(f))
