#!/usr/bin/env python3
# Synthetic v5 HVP pack: root -> dir "sub" -> {stored a.txt, LZO-literal b.txt}.
import struct, sys, os
out = sys.argv[1]
a = b'stored member'
b = b'lzo member data'
lzo = bytes([17 + len(b)]) + b + b'\x11\0\0'
pool = b'sub\0a.txt\0b.txt\0'
n = 4
tab_off = 20 + len(pool)
data_off = tab_off + 28 * n
def rec(h, fl, crc, size, no, x, y): return struct.pack('>7I', h, fl, crc, size, no, x, y)
t = rec(0, 4, 0, 0, 0, 1, 1)
t += rec(1, 4, 0, 0, 0, 2, 2)
t += rec(2, 0, 0, len(a), 4, data_off, len(a))
t += rec(3, 1, 0, len(b), 10, data_off + len(a), len(lzo))
hdr = struct.pack('>5I', 0x50000, 0, n, 0, len(pool)) + pool
open(os.path.join(out, 't.hvp'), 'wb').write(hdr + t + a + lzo)
