#!/usr/bin/env python3
# Synthetic NDP stereo DSP music: two 0x60 DSP headers, 4-byte interleaved frames, written as t.nds.
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
data = b''.join(a[i:i + 4] + b[i:i + 4] for i in range(0, len(a), 4))
out = b'NDP\0' + struct.pack('<IIII', 3, 0, 32000, 2) + struct.pack('<I', 0x10) + hdr() + hdr() + data
open(os.path.join(sys.argv[1], 't.nds'), 'wb').write(out)
