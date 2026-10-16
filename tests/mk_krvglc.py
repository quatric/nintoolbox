#!/usr/bin/env python3
# Synthetic Spyro DotD language table t.KRV: {u32 5, u32 12, ...}, then length-prefixed UTF-16LE strings.
import struct, sys, os
d = sys.argv[1]
out = struct.pack('<3I', 5, 12, 3) + bytes(0x34)
for t in ('Main Menu', 'New\nGame', 'Yes'):
    out += struct.pack('<I', len(t) + 1) + t.encode('utf-16le') + b'\0\0'
out += bytes(0x40)
open(os.path.join(d, 't.KRV'), 'wb').write(out)
