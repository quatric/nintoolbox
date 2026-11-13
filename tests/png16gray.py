#!/usr/bin/env python3
# Stdlib-only reader for 16 bit greyscale PNGs: prints the samples as little endian u16 on stdout
# (regress.sh compares them with a reference).
import struct, sys, zlib

d = open(sys.argv[1], 'rb').read()
pos, idat, ihdr = 8, b'', None
while pos < len(d):
    ln, typ = struct.unpack('>I4s', d[pos:pos + 8])
    if typ == b'IHDR':
        ihdr = struct.unpack('>IIBBBBB', d[pos + 8:pos + 8 + 13])
    elif typ == b'IDAT':
        idat += d[pos + 8:pos + 8 + ln]
    pos += 12 + ln
w, h, depth, ct = ihdr[:4]
assert depth == 16 and ct == 0, ihdr
bpp, stride = 2, w * 2
raw = zlib.decompress(idat)
prev = bytearray(stride)
out = bytearray()
for y in range(h):
    f = raw[y * (stride + 1)]
    line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
    for x in range(stride):
        a = line[x - bpp] if x >= bpp else 0
        b = prev[x]
        c = prev[x - bpp] if x >= bpp else 0
        if f == 1:
            line[x] = (line[x] + a) & 255
        elif f == 2:
            line[x] = (line[x] + b) & 255
        elif f == 3:
            line[x] = (line[x] + ((a + b) >> 1)) & 255
        elif f == 4:
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    prev = line
    for x in range(0, stride, 2):
        out += bytes([line[x + 1], line[x]])
sys.stdout.buffer.write(bytes(out))
