#!/usr/bin/env python3
# Synthetic EA Montreal t.loc (two strings, ids 0 and 5) and t.sub (one caption)
import struct, sys, os
d = sys.argv[1]
S = ['v1', 'Hi\tyou']
loch = b'LOCH' + struct.pack('<4I', 0x14, 1, 1, 0)
loci = b'LOCI' + struct.pack('<IIHHHH', 12 + 8, 1, 0, 0, 5, 1)
enc = [s.encode('utf-16-le') + b'\0\0' for s in S]
n = len(S)
off0 = 16 + 4 * n
offs = []
o = off0
for e in enc:
    offs.append(o); o += len(e)
locl = b'LOCL' + struct.pack('<III', o, 0, n) + b''.join(struct.pack('<I', x) for x in offs) + b''.join(enc)
open(os.path.join(d, 't.loc'), 'wb').write(loch + loci + locl)
def rec(a, b, text):
    body = struct.pack('<IIIIIIII', a, b, 0xa0, 0x150, 0x1e0, 0x54, 0xffffffff, 0x4343) if not text else \
           struct.pack('<IIIIIII', a, b, 0xa0, 0x150, 0x1e0, 0x54, 0xffffffff) + b'CC\0\0' + text.encode('utf-16-le')
    return b'ST00' + struct.pack('<I', 8 + len(body)) + body
open(os.path.join(d, 't.sub'), 'wb').write(rec(0, 16, '') + rec(16, 77, 'NAOMI\\nHi!'))
