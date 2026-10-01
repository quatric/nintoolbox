#!/usr/bin/env python3
"""Synthetic Natsume BIN archive: one tagged member, one raw member."""
import struct, sys, os
m = [b"mss\0" + b"abcd", b"raw-data"]
hdr = b"BIN\0" + struct.pack("<HHII", 1, 1, len(m), 0)
off = 0x10 + 16 * len(m)
tab = b""
for x in m:
    tab += struct.pack("<IIII", off, len(x), 0, 0)
    off += len(x)
open(os.path.join(sys.argv[1], "T.bin"), "wb").write(hdr + tab + b"".join(m))
