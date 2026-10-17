#!/usr/bin/env python3
# Synthetic Petz displacement track "t_dpgz": header, 3 keys of position/scale and a scalar channel.
import struct, sys, os
d = sys.argv[1]
f = lambda *v: struct.pack('>%df' % len(v), *v)
out = b'DPGZ' + struct.pack('>I', 15) + f(*([0.5] * 14))
out += struct.pack('>I', 3) + f(1.5)
out += struct.pack('>I', 3) + b''.join(f(i, 2 * i, 3 * i) for i in range(3))
out += struct.pack('>I', 3) + b''.join(f(1.0, 1.0, 2.0) for i in range(3))
out += struct.pack('>I', 3) + f(7.0, 8.0, 9.0) + struct.pack('>I', 0)
open(os.path.join(d, 't_dpgz'), 'wb').write(out)
