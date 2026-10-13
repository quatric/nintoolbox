#!/usr/bin/env python3
# Synthetic Rebellion Asura string table (PDC World Championship Darts, Wii) with three strings as t.str.
import struct, sys, os
d = sys.argv[1]
strs = ['Statistics', 'Line\none', '£%m']
body = b''
offs = []
for s in strs:
    b = s.encode('utf-8')
    offs.append(len(body))
    body += struct.pack('<HH', len(b), len(s)) + b
    body += b'\xff' * (-len(body) % 4)
f = struct.pack('<I', len(strs)) + b''.join(struct.pack('<I', o) for o in offs) + body
open(os.path.join(d, 't.str'), 'wb').write(f)
