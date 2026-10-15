#!/usr/bin/env python3
# Synthetic Exient t.bnk: two WIIADPCM members (0x100 / 0x120 bytes) behind a count + {offset, size, 0} table.
import struct, sys, os
m = [b'WIIADPCM' + bytes(range(0xf8)), b'WIIADPCM' + bytes(i & 255 for i in range(0x118))]
start = 4 + 12 * len(m) + 8
b = struct.pack('>I', len(m))
off = start
for x in m:
    b += struct.pack('>III', off, len(x), 0)
    off += len(x)
b += b'\xff' * 8
b += m[0] + m[1]
open(os.path.join(sys.argv[1], 't.bnk'), 'wb').write(b)
