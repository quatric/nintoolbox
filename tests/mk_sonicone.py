#!/usr/bin/env python3
# Synthetic Sonic Team .one archive: one PRS member "HELLO.BIN" = "ABAB" (2 literals + short copy)
import struct, sys, os
d = sys.argv[1]
packed = bytes([0x03, 0x41, 0x42, 0xfe])
ent = b'HELLO.BIN'.ljust(0x20, b'\0') + struct.pack('>4I', 0, 0x40, len(packed), 4)
hdr = struct.pack('>4I', 1, 0x10, 0x40, 0xffffffff)
open(os.path.join(d, 't.one'), 'wb').write(hdr + ent + packed)
