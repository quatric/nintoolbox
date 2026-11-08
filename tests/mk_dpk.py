#!/usr/bin/env python3
# Synthetic Simple Wii Series t.dpk: one bundle (Wpbb) with a stored part (a.bin) and a Wp16 part (b.bin = ABCDABCD).
import struct, sys, os
p0 = b'hello!!!'
stream = struct.pack('<I', 3) + b'AB' + b'CD' + struct.pack('<H', (2 << 5) | 0)
p1 = b'Wp16' + struct.pack('<I', 8) + stream
bundle = b'Wpbb' + struct.pack('<I', 2) + struct.pack('<IIII', 0x18, len(p0), 0x18 + len(p0), len(p1)) + p0 + p1
root = struct.pack('<5I', 0xffffffff, 0, 0xffffffff, 5, 0) + b'root\0'
node = struct.pack('<6I', 7, 0x800, len(bundle), 0, 0, 2) + b'root\\b\0'
files = struct.pack('<II', 0x18, 8) + b'a.bin\0' + struct.pack('<II', 0x18 + len(p0), 8) + b'b.bin\0'
tend = 12 + len(root) + len(node) + len(files)
head = struct.pack('<3I', tend, 2, 2) + root[0:0]
tab = head + root + node + files
data = tab.ljust(0x800, b'\0') + bundle
open(os.path.join(sys.argv[1], 't.dpk'), 'wb').write(data)
