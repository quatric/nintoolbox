#!/usr/bin/env python3
# Synthetic "PAK\3" archive (see extract/pak3.inc): one stored member and one LZSS member
# "abcabcabcabc" = literals a,b,c + match(len 9, dist 3), 16-bit LE control word, LSB first.
import struct, sys, os
out = sys.argv[1]
ctl = 0b1000  # bits 0-2 literal, bit 3 match
lz = b'\0\0\0\0' + struct.pack('<H', ctl) + b'abc' + bytes([8, 3])
plain = b'abcabcabcabc'
stored = b'stored member'
members = [('DIR\\A.TXT', lz, len(plain)), ('B.BIN', stored, len(stored))]
tab = b''; data = b''
off = 12 + 140 * len(members)
for name, body, size in members:
    tab += name.encode().ljust(128, b'\0') + struct.pack('<III', len(body), size, off + len(data))
    data += body
open(os.path.join(out, 't.pak'), 'wb').write(b'PAK\3' + struct.pack('<II', len(members), 1) + tab + data)
