#!/usr/bin/env python3
# Synthetic CSVT table file: t.vop with one 2x2 table "voice" (int, float / string, empty)
import struct, sys, os
d = sys.argv[1]
pool = b'hi,there\0'
data_off = 0x30
pool_off = data_off + 4 * 8
body = struct.pack('>IIII', 1, 7, 2, 0x40a00000) + struct.pack('>II', 3, pool_off) + struct.pack('>II', 0, 0)
hdr = b'CSVT' + struct.pack('>III', 0x10000, 1, 0x10) + b'voice'.ljust(16, b'\0') + struct.pack('>IIII', 2, 2, data_off, 0)
open(os.path.join(d, 't.vop'), 'wb').write(hdr + body + pool)
