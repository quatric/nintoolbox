#!/usr/bin/env python3
# Writes a synthetic Spyro .KCS buffer (one pose chunk of two bones) to DIR/t.KCS.
import struct, sys
cnt = 2
body = struct.pack('>IIfII', 0, 0, 1.0, 0, 0)
for bone, p in ((60, (1.0, 2.0, 3.0)), (228, (4.0, 5.0, 6.0))):
    body += struct.pack('>II4f3f', bone, 0, 0.0, 0.0, 0.0, 1.0, *p)
size = 16 + len(body) + 28
data = struct.pack('>IIII', size, size, 0, cnt) + body + b'\xcd' * 28
data += b'\xcd' * 8
data += b'\0' * (0x20000 - len(data))
open(sys.argv[1] + '/t.KCS', 'wb').write(data)
