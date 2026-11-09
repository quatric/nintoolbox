#!/usr/bin/env python3
# Synthetic Coresoft JPack .pak (+ jpack_t.log with names) holding one zlib-compressed NWTX RGB5A3 texture
# and one stored member (see extract/jpack.inc and extract/nwtx.inc).
import struct, sys, os, zlib
out = sys.argv[1]
tex = b'NWTX' + struct.pack('>7I', 3, 4, 4, 1, 32, 0, 0) + b''.join(struct.pack('>H', 0x8000 | i) for i in range(16))
m1 = zlib.compress(tex)
m2 = b'plain member'
def pad(b, a=0x10): return b + b'\0' * (-len(b) % a)
count = 2
hdr = struct.pack('>II', 1, count) + b'(C)2008 Coresoft, Inc.'.ljust(0x18, b'\0')
off = 0x20 + 20 * count
off = (off + 15) & ~15
ents = b''; data = b''
for h, body in ((0x11223344, m1), (0x55667788, m2)):
    ents += struct.pack('>QIII', h, 0x00020020, len(body), off + len(data))
    data += pad(body)
f = hdr + ents
f += b'\0' * (off - len(f)) + data
open(os.path.join(out, 't.pak'), 'wb').write(f)
open(os.path.join(out, 'jpack_t.log'), 'w').write(
    'JPack Log for t.pak\nFiles: 2\nHash: 11223344;  File: tex\\a.wtx; (Size: %d; Comp: %d)\n' % (len(tex), len(m1)))
