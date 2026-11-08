#!/usr/bin/env python3
# Synthetic Super Fruit Fall t.tdw (two RGB5A3 4x4 textures) and l.mee (12-byte-head string table).
import struct, sys, os
d = sys.argv[1]
def tex(name, w, h, fmt):
    data = bytes(w * h * 2)
    return name.encode().ljust(0x40, b'\0') + struct.pack('>6I', fmt, 3, 0, w, h, len(data)) + data + b'\0\0\0\0'
b = struct.pack('>I', 2) + tex('a', 4, 4, 5) + tex('b', 4, 4, 5)
open(os.path.join(d, 't.tdw'), 'wb').write(b)
keys = b'TID_A\0TID_B\0'
texts = b'Hello\nWorld\0Zur\xfcck\0'
open(os.path.join(d, 'l.mee'), 'wb').write(struct.pack('<III', 2, len(keys), len(texts)) + keys + texts)
