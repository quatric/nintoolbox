#!/usr/bin/env python3
"""Synthetic Opoona .atb collision mesh: one quad (2 triangles), empty grid."""
import struct, sys
out = sys.argv[1]
verts = [(-10, 0, -20), (-10, 0, 20), (10, 0, -20), (10, 0, 20)]
norms = [(0, -1, 0)]
tris = [(0, 3, 1, 0), (3, 0, 2, 0)]
voff = 0x38
noff = voff + 12 * len(verts)
toff = noff + 12 * len(norms)
goff = toff + 8 * len(tris)
loff = goff + 2048
end = loff + 6
hdr = struct.pack('>10I', end, 0x800000, 0x20200000, len(tris), toff, voff, noff, goff, loff, end)
hdr += struct.pack('>4f', 0.1, 0.05, -10.0, -20.0)
body = b''.join(struct.pack('>3f', *v) for v in verts + norms)
body += b''.join(struct.pack('>4H', *t) for t in tris)
body += struct.pack('>1024H', *([0] * 1024)) + struct.pack('>3H', 0, 1, 0xffff)
data = hdr + body
assert len(data) - 0 == end, (len(data), end)
open(out + '/room.atb', 'wb').write(data + struct.pack('>I', 4))
