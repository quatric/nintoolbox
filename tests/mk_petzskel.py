# Synthetic Petz skinned mesh header ("SMSH") with a 3 node skeleton (root with two children) as t_skin.
import struct, sys, os
d = sys.argv[1]
f = bytearray(b'SMSH' + struct.pack('>II', 2, 3)).ljust(0x40, b'\0')
def node(h, tx, qz, qw, kids):
    return struct.pack('>I3f3f4f3f3f4fI', h, 1, 1, 1, tx, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, qz, qw, kids)
f += node(0x11111111, 0.0, 0.0, 1.0, 2)
f += node(0x22222222, 0.5, 0.0, 1.0, 0)
f += node(0x33333333, 0.25, 0.6, 0.8, 0)
f += b'\0' * 0x20
open(os.path.join(d, 't_skin'), 'wb').write(bytes(f))
