#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii model wrapper ("\0DMR") around a stub BRRES as t.rmd.
import struct, sys, os
d = sys.argv[1]
off = 0x3a0
brres = b'bres\xfe\xff\x00\x00' + bytes(0x38)
f = bytearray(off + len(brres) + 0x20)
f[1:4] = b'DMR'
struct.pack_into('>I', f, 4, 0x38)
struct.pack_into('>I', f, 0x154, off)
struct.pack_into('>I', f, 0x15c, len(brres))
f[off:off + len(brres)] = brres
open(os.path.join(d, 't.rmd'), 'wb').write(bytes(f))
