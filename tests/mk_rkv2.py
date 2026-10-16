#!/usr/bin/env python3
# Synthetic Krome RKV2 archive t.rkv (two 0x40 byte headers, members from 0x80, 20 byte TOC records + name table).
import struct, sys, os
d = sys.argv[1]
files = [('a.txt', b'hello rkv2\n'), ('sub/b.bin', bytes(range(64)))]
body = b''
recs = []
names = b'\0'
off = 0x80
for name, data in files:
    recs.append((len(names), 0, len(data), off + len(body), 0x1234))
    names += name.encode() + b'\0'
    body += data + bytes((-len(data)) % 0x20)
toc = b''.join(struct.pack('<5I', *r) for r in recs) + names
toc_off = 0x80 + len(body)
head = b'RKV2' + struct.pack('<7I', len(files), 0, len(files), 0, toc_off, len(toc), 0x75f075f) + bytes(0x40 - 32)
open(os.path.join(d, 't.rkv'), 'wb').write(head + head + body + toc)
