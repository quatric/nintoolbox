#!/usr/bin/env python3
# Synthetic Rebellion Asura .sfx (stereo DSP) as t.sfx and RedSpark .rsd (encrypted header, mono stream) as t.rsd.
import struct, sys, os
d = sys.argv[1]
M = 0xffffffff
rotl = lambda x, r: ((x << r) | (x >> (32 - r))) & M
# --- Asura sfx
frames = 40
nib = frames * 16
blk = lambda fill: (bytes(range(32)) + struct.pack('>HH', 0, fill) + bytes(12) + bytes([fill]) + bytes([0x33]) * (frames * 8 - 1))
a, b = blk(0x11), blk(0x22)
gap = 0x30 + frames * 8
sfx = struct.pack('>IIIIIII', 0, 2, 2, nib, 44100, 0x20, 0x20 + gap).ljust(0x20, b'\0') + a + b'\0' * (gap - len(a)) + b
open(os.path.join(d, 't.sfx'), 'wb').write(sfx)
# --- RedSpark stream
plain = bytearray(0x30)
plain[0:8] = b'RedSpark'
sub = bytearray(0x5c)
sub[0x0c - 0x0:0x10 - 0x0] = struct.pack('>I', 32000)
sub[0x10:0x14] = struct.pack('>I', frames)
sub[0x1e] = 1
for i in range(16):
    sub[0x24 + 8 + 2 * i:0x24 + 8 + 2 * i + 2] = struct.pack('>H', i)
data_off = 0x30 + len(sub)
data = bytes([0x55]) + bytes([0x77]) * (frames * 8 - 1)
struct.pack_into('>I', plain, 0x18, data_off)
struct.pack_into('>I', plain, 0x20, len(data))
struct.pack_into('>H', plain, 0x1c, 0)
struct.pack_into('>H', plain, 0x1e, 0x0909)
words = plain + sub
K = 0x45656453
out = bytearray()
key = K
out += struct.pack('>I', struct.unpack('>I', bytes(words[0:4]))[0] ^ K)
key = rotl(K, 11)
for i in range(4, len(words), 4):
    key = (rotl(key, 3) + key) & M
    out += struct.pack('>I', struct.unpack('>I', bytes(words[i:i + 4]))[0] ^ key)
open(os.path.join(d, 't.rsd'), 'wb').write(bytes(out) + data)
