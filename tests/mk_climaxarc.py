#!/usr/bin/env python3
# Synthetic Climax data.arc: one zlib member and one stored member (usize 0)
import struct, sys, os, zlib
d = sys.argv[1]
a = b'hello world ' * 10
za = zlib.compress(a)
b = b'\xff\xd8\xff\xe0' + bytes(20)
first = 0x800
offa = first
offb = offa + 0x800
tab = struct.pack('<4I', 0x11111111, offa, len(za), len(a)) + struct.pack('<4I', 0x22222222, offb, len(b), 0)
hdr = struct.pack('<4I', 0xfa10, 2, first, 0) + tab
img = hdr.ljust(first, b'\0') + za.ljust(0x800, b'\0') + b
open(os.path.join(d, 't.arc'), 'wb').write(img)
