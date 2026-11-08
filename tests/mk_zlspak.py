#!/usr/bin/env python3
# Synthetic PAK\0 package: one !ZLS (zlib) member and one stored member. t.dat
import struct, sys, os, zlib
m1 = b'zls member payload'
z = b'!ZLS' + struct.pack('<I', len(m1)) + zlib.compress(m1)
m2 = b'stored member'
head = 8 + 12 * 2
o1 = head
o2 = o1 + len(z)
d = b'PAK\0' + struct.pack('>I', 2) + struct.pack('>III', 0x00000011, o1, len(z)) + struct.pack('>III', 0x00000022, o2, len(m2)) + z + m2
open(os.path.join(sys.argv[1], 't.dat'), 'wb').write(d)
