#!/usr/bin/env python3
# Synthetic FMOD Designer event file t.fev: "FEV1" header and a few length-prefixed names.
import struct, sys, os
def s(n):
    n = n.encode() + b'\0'
    return struct.pack('<I', len(n)) + n
body = struct.pack('<IIII', 0x3200, 0x100, 0x100, 1) + s('AX03s') + bytes(8) + s('sfx') + bytes(5) + s('AX03_amb_hiss') + bytes(16)
open(os.path.join(sys.argv[1], 't.fev'), 'wb').write(b'FEV1' + body)
