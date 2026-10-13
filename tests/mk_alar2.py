#!/usr/bin/env python3
# Synthetic Ganbarion ALAR (big-endian, Pandora's Tower): two tagged members
import struct, sys, os
d = sys.argv[1]
m0 = b'ALIG' + bytes(12)
m1 = b'ALMS' + b'payload!' + bytes(4)
cnt = 2
o0 = 0x10 + 16 * cnt
o1 = o0 + len(m0)
hdr = b'ALAR' + bytes([2, 0xe1]) + struct.pack('>H', cnt) + bytes(8)
recs = bytes([0x42, 0x02, 0x00, 0x02]) + struct.pack('>3I', o0, len(m0), 0x80000001) + \
       bytes([0x44, 0x02, 0x00, 0x5d]) + struct.pack('>3I', o1, len(m1), 0x80000001)
open(os.path.join(d, 't.aar'), 'wb').write(hdr + recs + m0 + m1)
