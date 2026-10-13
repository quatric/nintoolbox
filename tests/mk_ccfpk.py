#!/usr/bin/env python3
# Synthetic CyberConnect2-style fpack .fpk: one stored member and one PRS (MSB-first) member
import struct, sys, os
d = sys.argv[1]
stored = b'STORED-MEMBER-DATA'
# PRS stream for "ABABAB", control bits MSB first: 1 1 (literals A B) 0 0 1 0 (short copy, length 2+2, offset
# byte 0xfe = -2) 0 1 (long copy with a == 0 and length byte 0 = end) -> 0b11001001
packed = bytes([0xc9, 0x41, 0x42, 0xfe, 0x00, 0x00, 0x00])
unpacked = b'ABABAB'
tab = 0x10
n = 2
data0 = tab + 0x30 * n
data1 = data0 + len(stored)
def ent(name, off, cs, us): return name.encode().ljust(0x24, b'\0') + struct.pack('>3I', off, cs, us)
body = ent('dir/stored.bin', data0, len(stored), len(stored)) + ent('packed.bin', data1, len(packed), len(unpacked))
total = data1 + len(packed)
img = struct.pack('>4I', 0x1234, n, tab, total) + body + stored + packed
open(os.path.join(d, 't.fpk'), 'wb').write(img)
