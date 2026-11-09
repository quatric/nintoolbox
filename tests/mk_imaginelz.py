#!/usr/bin/env python3
# Synthetic Ubisoft Imagine LZSS file (see extract/imaginelz.inc): "abcabcabcabc" as literals a,b,c plus a
# match {0xee, 0xf6} = ring position 4078, length 9.
import struct, sys, os
out = sys.argv[1]
stream = bytes([0x07]) + b'abc' + bytes([0xee, 0xf6])
f = b'\x21\x43\x65\x87' + struct.pack('<II', len(stream), 12) + stream
open(os.path.join(out, 'x.wiz'), 'wb').write(f)

# a compressed .wsp sprite: LE {format 4 (RGB565), 4, 4}, 0xcc padding to 0x20, one 4x4 GX tile
raw = struct.pack('<III', 4, 4, 4) + b'\xcc' * 20 + b''.join(struct.pack('>H', 0xf800 if i < 8 else 0x07e0) for i in range(16))
st = b''
for k in range(0, len(raw), 8):
    chunk = raw[k:k + 8]
    st += bytes([(1 << len(chunk)) - 1]) + chunk
open(os.path.join(out, 'y.wsp'), 'wb').write(b'\x21\x43\x65\x87' + struct.pack('<II', len(st), len(raw)) + st)
