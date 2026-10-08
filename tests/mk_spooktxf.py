#!/usr/bin/env python3
# Synthetic SPHC locale string table: u32 count, u32 offsets, NUL-terminated UTF-8.
import struct, sys, os
strs = ['English'.encode(), 'Français'.encode(), b'two\nlines']
base = 4 + 4 * len(strs)
offs, blob = [], b''
for s in strs:
    offs.append(base + len(blob))
    blob += s + b'\0'
open(os.path.join(sys.argv[1], 't.txf'), 'wb').write(struct.pack('<I', len(strs)) + b''.join(struct.pack('<I', o) for o in offs) + blob)
