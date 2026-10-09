#!/usr/bin/env python3
# Synthetic Punchers Impact MYSPD (U-Sing): 8-byte channels of IMA code 4 as t.myspd.
import struct, sys, os
d = sys.argv[1]
cs = 8
f = bytearray(0x20)
struct.pack_into('>II', f, 0, cs, 32000)
f += bytes([0x44]) * cs + bytes([0x44]) * cs
open(os.path.join(d, 't.myspd'), 'wb').write(bytes(f))
