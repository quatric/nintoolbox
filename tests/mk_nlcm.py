#!/usr/bin/env python3
# Synthetic NLCM index + data file: three files at 0x800-aligned offsets of T.dat, listed by T.bin.
import struct, sys, os
out = sys.argv[1]
files = [b'hello' * 100, b'\x00' * 3000 + b'abc', b'zz' * 17]
dat = bytearray()
ents = []
for f in files:
    ents.append((len(f), len(dat)))
    dat += f
    dat += b'\0' * ((-len(dat)) % 0x800)
open(os.path.join(out, 'T.dat'), 'wb').write(bytes(dat))
h = b'NLCM' + struct.pack('>IIII', 0x38, 0x14, len(files), 1) + b'T.dat'.ljust(0x24, b'\0')
assert len(h) == 0x38
for s, o in ents:
    h += struct.pack('>IIII', s, 0, o, 0)
open(os.path.join(out, 'T.bin'), 'wb').write(h)
