#!/usr/bin/env python3
# Synthetic Shade/Level-5 containers: scn.bin and an mcb0.bln/mcb1.bln pair
# holding one ShadeLz member (literals, a byte run and a back reference).
import struct, sys, os
out = sys.argv[1]
payload = b'abcdefgh' + b'x' * 10 + b'abcdef'
body = bytes([8]) + b'abcdefgh' + bytes([0x40 | 6, ord('x')]) + bytes([0x80 | (2 << 5) | 0, 18])
comp = b'\xfc\xaa\x55\xa7' + struct.pack('<II', len(payload), len(body)) + body

pad, shift = 0x40, 8
hdr = struct.pack('<5I', 1, pad, 1, shift, (1 << shift) - 1) + struct.pack('<I', (1 << shift) | len(comp))
grp = hdr.ljust(0x40, b'\0') + comp
open(os.path.join(out, 'scn.bin'), 'wb').write(grp)

m1 = struct.pack('<III', 0, 0, len(comp)) + comp
open(os.path.join(out, 'mcb1.bln'), 'wb').write(m1)
open(os.path.join(out, 'mcb0.bln'), 'wb').write(struct.pack('<III', 1, 0, len(m1)))
