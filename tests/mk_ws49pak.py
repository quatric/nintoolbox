#!/usr/bin/env python3
# Synthetic 49Games .pak (Summer/Winter Athletics): 0xDEADBEEF header, 40-byte records, one stored member
# (WSBL magic) and one packed member (stored size < unpacked size).
import struct, sys, os
out = sys.argv[1]
members = [(0xaabbccdd, b'WSBL stored member!!', 20), (0x11223344, b'\x3c\xce packed', 99)]
idx = 0x14 + 40 * len(members)
recs = b''; body = b''
for h, d, usz in members:
    recs += struct.pack('<10I', h, 0x1234, idx + len(body), 0, len(d), 0, usz, 0, 0, 0)
    body += d
open(os.path.join(out, 't.pak'), 'wb').write(struct.pack('<5I', 0xdeadbeef, len(members), idx, 1, 0) + recs + body)
