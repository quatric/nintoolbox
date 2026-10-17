#!/usr/bin/env python3
# Synthetic MT Framework parameter tables: t.thk (2 groups, 1 and 2 items), t.atk and t.col.
import struct, sys, os
d = sys.argv[1]
def item(k): return b''.join(struct.pack('>f', 1.5 * (k + i)) if i % 2 else struct.pack('>I', k + i) for i in range(44))
thk = bytearray(b'\0KHT' + struct.pack('>HHI', 0x1415, 0, 0x10) + bytes(4))
thk += bytes([1, 0, 0, 0]) + struct.pack('>I', 0x30) + bytes(8)
thk += bytes([2, 0, 0, 0]) + struct.pack('>I', 0x30 + 176) + bytes(8)
for k in range(3): thk += item(k)
open(os.path.join(d, 't.thk'), 'wb').write(thk)
for ext, mg, ver in (('atk', b'\0KTA', 0x1021), ('col', b'\0LOC', 0x1021)):
    b = mg + struct.pack('>HH', ver, 2) + b'NONAME'.ljust(16, b'\0') + bytes(range(32))
    open(os.path.join(d, 't.' + ext), 'wb').write(b)
