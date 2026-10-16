#!/usr/bin/env python3
# Synthetic two-part DC2 model: part 0 has a triangle strip (0x9b) of three vertices, part 1 a triangle list (0x93) of
# three vertices, written as m.dcm (parts' arrays come first, then one run of primitive records per part).
import struct, sys, os
out = sys.argv[1]
def block(stride, rows):
    return struct.pack('>IBI', 0, stride, len(rows)) + b''.join(rows)
pos = [struct.pack('>3f', *v) for v in ((0, 0, 0), (1, 0, 0), (0, 1, 0))]
nrm = [struct.pack('>3f', 0, 0, 1)]
uv = [struct.pack('>2f', *v) for v in ((0, 0), (1, 0), (0, 1))]
strip = b'\x9b' + struct.pack('>H', 3) + b''.join(struct.pack('>3H', i, 0, i) for i in range(3))
tris = b'\x93' + struct.pack('>H', 3) + b''.join(struct.pack('>3H', i, 0, i) for i in range(3))
arrays = block(12, pos) + block(12, nrm) + block(8, uv)
data = b'DC2\0' + b'\0' * (0x3b - 4) + arrays + struct.pack('>QQQ', 3, 0, 0x200)[:0x20] + arrays
data += b'\0' * 0x10 + strip + b'\0' * 0x20 + tris + b'\0' * 0x40
open(os.path.join(out, 'm.dcm'), 'wb').write(data)
