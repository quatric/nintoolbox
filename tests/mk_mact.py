#!/usr/bin/env python3
# Synthetic Petz action table t_mact: name "bip01", 2 entries.
import struct, sys, os
d = sys.argv[1]
n = 2
out = b'MACT' + struct.pack('>I', 5) + b'bip01' + struct.pack('>I', n)
out += b''.join(struct.pack('>I', 0x1000 + i) for i in range(n))
out += b''.join(struct.pack('>BBI', i % 5, 1, 0xabc00 + i) for i in range(3 * n + 1))
out += bytes(16)
out += b''.join(struct.pack('>Hhh', 0x80 + i, -1, 104 if i == n - 1 else -1) for i in range(n))
out += bytes(16)
open(os.path.join(d, 't_mact'), 'wb').write(out)
