#!/usr/bin/env python3
# Synthetic Ubisoft Imagine LZSS file (see extract/imaginelz.inc): "abcabcabcabc" as literals a,b,c plus a
# match {0xee, 0xf6} = ring position 4078, length 9.
import struct, sys, os
out = sys.argv[1]
stream = bytes([0x07]) + b'abc' + bytes([0xee, 0xf6])
f = b'\x21\x43\x65\x87' + struct.pack('<II', len(stream), 12) + stream
open(os.path.join(out, 'x.wiz'), 'wb').write(f)
