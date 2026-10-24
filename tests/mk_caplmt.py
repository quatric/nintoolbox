#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii motion bank ("\0TML") with one 73 frame motion as t.lmt:
# track 0 is a type 5 (8 bit) translation, track 1 a type 6 (14 bit fixed point) rotation.
import struct, sys, os
d = sys.argv[1]
f = bytearray(0x220)
f[1:4] = b'TML'
f[5] = 0x38
struct.pack_into('>H', f, 6, 2)
struct.pack_into('>I', f, 8, 0)
struct.pack_into('>I', f, 12, 0x20)
struct.pack_into('>IIII', f, 0x20, 0x180, 2, 73, 0)
# track: bone, joint, usage, buffer type, weight, size, offset, reference[4], extra offset
struct.pack_into('>BBBBfII4fI', f, 0x180, 6, 0, 1, 5, 1.0, 8, 0x1d0, 1.0, 10.0, 2.0, 1.0, 0x1f0)
struct.pack_into('>BBBBfII4fI', f, 0x180 + 0x24, 7, 0, 0, 6, 1.0, 16, 0x1d8, 0.0, 0.0, 0.0, 1.0, 0)
# type 5 keys {x, y, z, frames to next}: y 8 -> 248 over 72 frames
f[0x1d0:0x1d8] = bytes([8, 8, 8, 72, 8, 248, 8, 0])
# multiplier / base: y runs 10..40
struct.pack_into('>8f', f, 0x1f0, 0.0, 30.0, 0.0, 0.0, 1.0, 10.0, 2.0, 0.0)
# type 6 keys: frames to next + four signed 14 bit values with 12 fractional bits, w 0x1000 = 1.0
def key6(dt, x, y, z, w):
    v = 0
    for c in (x, y, z, w):
        v = v << 14 | (c & 0x3fff)
    return struct.pack('>Q', dt << 56 | v)
f[0x1d8:0x1d8 + 8] = key6(36, 0, 0, 0, 4096)
f[0x1d8 + 8:0x1d8 + 16] = key6(0, -2048, 0, 0, 0)
open(os.path.join(d, 't.lmt'), 'wb').write(bytes(f))
