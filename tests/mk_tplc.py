#!/usr/bin/env python3
# Synthetic "10 Minute Solution" LZSS container: an 8x8 I8 TPL stored as
# literals only, written as t.tplc.
import struct, sys, os
out = sys.argv[1]
img = struct.pack('>HHIIIIIIfBBBB', 8, 8, 1, 0x40, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = struct.pack('>IIII', 0x0020af30, 1, 0xc, 0x14) + struct.pack('>I', 0) + img
tpl += b'\0' * (0x40 - len(tpl)) + bytes(range(0, 256, 4))
raw = struct.pack('>I', len(tpl) + 4) + tpl
toks = []
i = 0
while i < len(raw):
    toks.append(('L', raw[i])); i += 1
body = bytearray(); flags = 0; n = 0; buf = bytearray()
def flush():
    global flags, n, buf, body
    if n: body += bytes([flags]) + buf
    flags = 0; n = 0; buf = bytearray()
for kind, v in toks:
    flags |= 1 << n; buf.append(v); n += 1
    if n == 8: flush()
flush()
open(os.path.join(out, 't.tplc'), 'wb').write(bytes(body))
