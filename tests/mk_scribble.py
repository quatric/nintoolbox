#!/usr/bin/env python3
# Synthetic Scribblenauts index.bin + one .p: a zlib member, a raw member, an absent entry.
import struct, sys, os, zlib
out = sys.argv[1]
z = zlib.compress(b'scribble zlib member') + b'\x06'
raw = b'raw member'
pk = (b'\0' * 0x40) + z + raw
open(os.path.join(out, 'a.p'), 'wb').write(pk)
ent = [struct.pack('>BBIII', 0, 6, 0x40, len(z), 20),
       struct.pack('>BBIII', 0, 0, 0x40 + len(z), len(raw), len(raw)),
       struct.pack('>BBIII', 255, 0, 0, 0, 0)]
idx = struct.pack('>I', len(ent)) + b''.join(ent) + struct.pack('>I', 1) + b'\x03a.p'
open(os.path.join(out, 'index.bin'), 'wb').write(idx)
