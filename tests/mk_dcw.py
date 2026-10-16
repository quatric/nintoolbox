#!/usr/bin/env python3
# Synthetic Speed 2 world t.dcw: one position array (4), one UV array (4) and one 0x92 triangle-list run of six
# {position, normal, UV} index triples forming a quad.
import struct, sys, os
d = sys.argv[1]
out = bytearray(b'DC2\0' + bytes(0x2c))
out += struct.pack('>IBI', 0, 12, 4) + b''.join(struct.pack('>3f', *p) for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)))
out += struct.pack('>IBI', 0, 8, 4) + b''.join(struct.pack('>2f', *p) for p in ((0, 0), (1, 0), (0, 1), (1, 1)))
out += bytes(24)
idx = (0, 1, 2, 3, 2, 1)
out += bytes([0x92]) + struct.pack('>H', 6) + b''.join(struct.pack('>3H', i, 0, i) for i in idx)
out += bytes(0x80)
open(os.path.join(d, 't.dcw'), 'wb').write(out)
