#!/usr/bin/env python3
# Synthetic Petz level container t.ozzy: u32 0, two zlib chunks {u32 raw, u32 comp, data}, 4 byte trailer. The stream
# holds a MESH node and one GEOM of three vertices (f32 positions, s8 normals, u8 texcoords, GX display list) plus a
# diffuse texture path. Writes the inflated stream as t.expect.
import struct, sys, os, zlib
d = sys.argv[1]
def s(t):
    return struct.pack('>I', len(t)) + t.encode()
def block(n, stride, desc, data):
    return struct.pack('>IIB', n, 0, stride) + bytes(desc) + struct.pack('>I', len(data)) + data
pos = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
P = b''.join(struct.pack('>3f', *v) for v in pos)
N = bytes([0, 0, 64]) * 3
U = bytes([0, 0, 255, 0, 0, 255])
dl = bytes([0x94]) + struct.pack('>H', 3)
for i in range(3):
    dl += P[i * 12:i * 12 + 12] + N[i * 3:i * 3 + 3] + U[i * 2:i * 2 + 2]
dl += bytes(3)
geom = b'GEOM' + struct.pack('>4I', 4, 3, 0, 7)
geom += block(3, 12, [0, 1, 0, 3, 0, 0], P) + block(3, 3, [0, 2, 1, 3, 0, 6], N) + block(3, 2, [0, 5, 4, 2, 0, 8], U)
geom += struct.pack('>I', len(dl)) + dl + struct.pack('>II', 1, 0)[:4] + s('3d/texture/test/tx_t')
stream = b'\0\0\0\0LV3D' + s('3d/scenemanager/test') + b'MESH' + struct.pack('>II', 1, 1) + s('3d/geometry/test/sm_t_0') + geom + bytes(64)
open(os.path.join(d, 't.expect'), 'wb').write(stream)
out = struct.pack('>I', 0)
for i in range(0, len(stream), 0x4000):
    c = stream[i:i + 0x4000]
    z = zlib.compress(c)
    out += struct.pack('>II', len(c), len(z)) + z
open(os.path.join(d, 't.ozzy'), 'wb').write(out + b'\x01\x02\x03\x04')
