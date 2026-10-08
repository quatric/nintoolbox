#!/usr/bin/env python3
# Synthetic EA LOCH/LOCL locale table (two UTF-16LE strings), written as t.loc.
import struct, sys, os
strs = ['Hello', 'Caf\u00e9\nBar']
tbl_end = 0xc + 4 * 3 + 2      # entry 0 points just past the offset list (odd, like the real files)
body = b''
offs = []
cur = tbl_end + 6              # a few bytes of the (unused) pointer table
for s in strs:
    offs.append(cur)
    b = s.encode('utf-16le') + b'\0\0'
    body += b
    cur += len(b)
chunk = b'LOCL' + struct.pack('<II', 0, 0)
chunk += struct.pack('<I', tbl_end) + b''.join(struct.pack('<I', o) for o in offs)
chunk += b'\0' * (tbl_end + 6 - len(chunk)) + body
chunk = chunk[:4] + struct.pack('<I', len(chunk)) + chunk[8:]
out = b'LOCH' + struct.pack('<IIII', 0x14, 0, 1, 0x14) + chunk
open(os.path.join(sys.argv[1], 't.loc'), 'wb').write(out)
