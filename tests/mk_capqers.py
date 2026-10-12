#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii sound event request table ("QERS", version 19) as t.qers.
import struct, sys, os

d = sys.argv[1]
count = 2
strings = b'_common_sound\0sound/se/pl/pl00\0'
f = bytearray(128 + count * 144 + len(strings))
f[0:4] = b'QERS'
struct.pack_into('>II', f, 4, 19, count)

# record 0
struct.pack_into('>II', f, 128 + 24, 0x101, 0x5)
# record 1
struct.pack_into('>II', f, 128 + 144 + 24, 0x102, 0x8)

# string table
f[128 + count * 144 : 128 + count * 144 + len(strings)] = strings

open(os.path.join(d, 't.qers'), 'wb').write(bytes(f))
