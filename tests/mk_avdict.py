#!/usr/bin/env python3
# Synthetic Avalanche DICT (Disney's Bolt): header, a table of non-text words, then 20 NUL-terminated strings.
import struct, sys, os
d = sys.argv[1]
strs = [b'Hello\nthere', b'ID_001', b'Bolt\xe2\x80\xa6 speak!'] + [b'line %d' % i for i in range(17)]
f = b'DICT' + struct.pack('<IIIIIII', 0x2000, 0x1234, 0x13, 0x1048, 1, 0xc37f, 1) + bytes(0x28)
f += struct.pack('<III', 0x11620, 0x4f, 0) * 8 + bytes(4)
f += b'\0'.join(strs) + b'\0'
open(os.path.join(d, 't.dct'), 'wb').write(f)
