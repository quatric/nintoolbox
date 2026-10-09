#!/usr/bin/env python3
# Synthetic Next Level Games files: t.sanim.zlib (u32 BE size + zlib) and t.loc ("NLOC" string table).
import struct, sys, os, zlib
d = sys.argv[1]
payload = b'\x80\x01p\x00' + bytes(range(60))
open(os.path.join(d, 't.sanim.zlib'), 'wb').write(struct.pack('>I', len(payload)) + zlib.compress(payload))
strs = ['SP1', 'Café あ']
pool = b''
offs = []
for s in strs:
    offs.append(len(pool) // 2)
    pool += s.encode('utf-16be') + b'\0\0'
tbl = b''.join(struct.pack('>II', 100 + i, o) for i, o in enumerate(offs))
out = b'NLOC' + struct.pack('>IIII', 1, 0x12345678, len(strs), 1) + tbl + pool
open(os.path.join(d, 't.loc'), 'wb').write(out)
