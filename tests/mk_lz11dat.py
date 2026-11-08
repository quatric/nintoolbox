#!/usr/bin/env python3
# Synthetic resource.dat + resource.tbl: slot 0 stored ("storedblock!!"), slot 2 LZ11 literal-only ("hello lz11 world").
import struct, sys, os
def lz11_lit(d):
    out = bytearray(b'\x11' + len(d).to_bytes(3, 'little'))
    for i in range(0, len(d), 8):
        chunk = d[i:i + 8]
        out.append(0)
        out += chunk
    return bytes(out)
a = b'storedblock!!!'
b = b'hello lz11 world'
cb = lz11_lit(b)
dat = bytearray(b'dummy'.ljust(0x20, b'\0'))
oa = len(dat); dat += a; dat += b'\0' * (-len(dat) % 0x20)
ob = len(dat); dat += cb; dat += b'\0' * (-len(dat) % 0x20)
tbl = bytearray(0x20 + 12 * 4)
struct.pack_into('>III', tbl, 0x20, oa, len(a), len(a))
struct.pack_into('>III', tbl, 0x20 + 24, ob, len(cb), len(b))
d = sys.argv[1]
open(os.path.join(d, 'resource.dat'), 'wb').write(bytes(dat))
open(os.path.join(d, 'resource.tbl'), 'wb').write(lz11_lit(bytes(tbl)))
