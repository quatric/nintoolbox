#!/usr/bin/env python3
# Synthetic Tornado Outbreak t.bgw: "ZLIB", big-endian size, zlib stream.
import struct, sys, os, zlib
payload = struct.pack('>II', 4, 0x2378) + b'LCTX' * 64
open(os.path.join(sys.argv[1], 't.bgw'), 'wb').write(b'ZLIB' + struct.pack('>I', len(payload)) + zlib.compress(payload))
