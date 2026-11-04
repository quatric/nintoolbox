#!/usr/bin/env python3
# Synthetic "PAKb" archive (Secret Files): count + {hash, offset, size} records, stored payloads.
import struct, sys, os
out = sys.argv[1]
members = [(0x11223344, b'func table member'), (0x55667788, b'\x01\x02\x03\x04 raw bytes')]
base = 8 + 12 * len(members)
recs = b''; body = b''
for h, d in members:
    recs += struct.pack('>III', h, base + len(body), len(d)); body += d
open(os.path.join(out, 't.pak'), 'wb').write(b'PAKb' + struct.pack('>I', len(members)) + recs + body)
