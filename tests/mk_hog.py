#!/usr/bin/env python3
# Synthetic HOG archive with two members, written as t.hog.
import struct, sys, os
out = sys.argv[1]
files = [('DIR\\A.TXT', b'hello hog'), ('B.BIN', b'\x01\x02\x03\x04')]
names = b''.join(n.encode() + b'\0' for n, _ in files)
base = 0x20 + 16 * len(files)
noff = base
doff = base + len(names)
tab = b''
for n, d in files:
    tab += struct.pack('<IIII', noff, doff, len(d), 0)
    noff += len(n) + 1
    doff += len(d)
hdr = struct.pack('<HH7I', 1, 2, 0x20, 0, 0, len(files), len(names), 0, 0)
open(os.path.join(out, 't.hog'), 'wb').write(hdr + tab + names + b''.join(d for _, d in files))
