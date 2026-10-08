#!/usr/bin/env python3
# Synthetic Messiah-engine "PDA" .bra archive with one deflated member.
import struct, sys, os, zlib
out = sys.argv[1]
payload = b'bra member payload ' * 8
c = zlib.compressobj(9, zlib.DEFLATED, -15)
comp = c.compress(payload) + c.flush()
member = struct.pack('<4I', len(payload), len(comp), 0, 0x159e0006) + comp
name = b'Dir\\a.txt'
tab_off = 16 + len(member)
rec = struct.pack('<IIIIHHI', 0, 0, len(member), len(payload), len(name), 0x20, 16) + name
open(os.path.join(out, 't.bra'), 'wb').write(b'PDA\0' + struct.pack('<III', 2, tab_off, 1) + member + rec)

# "3df " container embedding the same kind of deflate member at 0x1a0.
t = b'3df\0' + struct.pack('<I', 29) + b'\0' * 0x198
open(os.path.join(out, 't.3df'), 'wb').write(t + member)
