#!/usr/bin/env python3
# Synthetic Pet Vet style package t.dat: one zlib member and one stored member.
import struct, sys, os, zlib
m1 = b'compressed member text'
z = zlib.compress(m1)
m2 = b'stored member'
base = 0x40 + 0x80 * 2
o1 = base
o2 = (o1 + len(z) + 15) & ~15
def ent(name, off, size, fl, us):
    return name.ljust(0x68, b'\0') + struct.pack('>6I', off, size, fl, 0, us, 0)
head = b'TAPSeaWii'.ljust(0x20, b'\0') + struct.pack('>II', 2, 0x10000) + bytes(0x18)
d = head + ent(b'data\\wii\\a\\one.txt', o1, len(z), 1, len(m1)) + ent(b'data\\wii\\two.txt', o2, len(m2), 0, len(m2))
d = d.ljust(o1, b'\0') + z
d = d.ljust(o2, b'\0') + m2
open(os.path.join(sys.argv[1], 't.dat'), 'wb').write(d)
