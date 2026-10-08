#!/usr/bin/env python3
# Synthetic DC2 model: one triangle-strip of three vertices, written as m.dcm.
import struct, sys, os
out = sys.argv[1]
def block(stride, rows):
    return struct.pack('>IBI', 0, stride, len(rows)) + b''.join(rows)
pos = [struct.pack('>3f', *v) for v in ((0, 0, 0), (1, 0, 0), (0, 1, 0))]
nrm = [struct.pack('>3f', 0, 0, 1)]
uv = [struct.pack('>2f', *v) for v in ((0, 0), (1, 0), (0, 1))]
prim = b'\x9b' + struct.pack('>H', 3) + b''.join(struct.pack('>3H', i, 0, i) for i in range(3))
data = b'DC2\0' + b'\0' * (0x3b - 4) + block(12, pos) + block(12, nrm) + block(8, uv)
data += struct.pack('>III', 1, 0, len(prim)) + prim + b'\0' * 0x40
open(os.path.join(out, 'm.dcm'), 'wb').write(data)
