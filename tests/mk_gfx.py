#!/usr/bin/env python3
# Synthetic compressed Scaleform movie: t.gfx = "CFX" 9 + length + zlib(body)
import struct, sys, os, zlib
d = sys.argv[1]
body = bytes(range(64)) * 4
open(os.path.join(d, 't.gfx'), 'wb').write(b'CFX\x09' + struct.pack('<I', 8 + len(body)) + zlib.compress(body))
