#!/usr/bin/env python3
# Synthetic Traveller's Tales IDSP (version 2/0xd2: 2 channels, header at 0x20, 0x10-byte interleave),
# written as t.gcm.
import struct, sys, os
frames = 4
nib = frames * 16
def hdr():
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, frames * 14, nib, 32000)
    struct.pack_into('>III', h, 0x10, 2, nib - 1, 2)
    return bytes(h)
a = bytes(range(0x10, 0x10 + frames * 8))
b = bytes(range(0x80, 0x80 + frames * 8))
il = 0x10
data = b''.join(a[i:i + il] + b[i:i + il] for i in range(0, len(a), il))
out = b'IDSP' + struct.pack('>IIII', 2, 0xd2, il, 0) + b'\0' * 12 + hdr() + hdr() + data
out += b'\0' * (0x100 - len(out)) if len(out) < 0x100 else b''
open(os.path.join(sys.argv[1], 't.gcm'), 'wb').write(out)
