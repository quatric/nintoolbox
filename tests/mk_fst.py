#!/usr/bin/env python3
# Synthetic Crazy Machines FAST archive: one zlib object holding a PCM16 sample.
import struct, sys, os, zlib
out = sys.argv[1]
pcm = struct.pack('<8h', 0, 100, 200, 300, 200, 100, 0, -100)
obj = struct.pack('>Q', 7) + struct.pack('>Q', 0) + struct.pack('>IIIIII', 1, 32000, 16, 2, len(pcm), len(pcm)) + pcm
hdr = b'FAST' + struct.pack('<III', 0x30000, 1, 0x10000) + b'\0' * 0x30
open(os.path.join(out, 'a.fst'), 'wb').write(hdr + b'\x04Test' + zlib.compress(obj, 9))
