#!/usr/bin/env python3
# Synthetic Ubisoft DARE sound package t2.pk (Just Dance layout, 0x22000d): one header BAO for a stereo 32 kHz
# 16 bit little-endian PCM stream of 4 frames kept in the external stream file s.spk (data after its 0x28 byte BAO header).
import struct, sys, os
d = sys.argv[1]
hdr = bytearray(0xa8 + 0x80)
struct.pack_into('>I', hdr, 0, 0x0122000d)
struct.pack_into('<I', hdr, 4, 0x28)
struct.pack_into('>I', hdr, 0x20, 0x20000000)
h = 0x28
for off, val in ((0, 0x2001), (4, 1), (8, 16), (0x1c, 0x2002), (0x28, 1), (0x34, 0), (0x44, 2), (0x4c, 32000), (0x54, 4),
                 (0x5c, 0), (0x64, 1), (0x68, 0), (0x6c, 0), (0x74, 0)):
    struct.pack_into('>I', hdr, h + off, val)
index = struct.pack('<II', 0x2001, len(hdr))
ext = 0x40 + len(index) + len(hdr)
strs = b's.spk\0'
res = struct.pack('<II', 1, len(strs)) + strs + struct.pack('<IIII', 0x2002, 0, 0, 0x28 + 16)
head = bytearray(0x40)
head[0:4] = struct.pack('>I', 0x0122000d)
struct.pack_into('<II', head, 4, len(index), ext)
open(os.path.join(d, 't2.pk'), 'wb').write(bytes(head) + index + bytes(hdr) + res)
spk = bytearray(0x28) + struct.pack('<8h', 1, -2, 300, -400, 5000, -6000, 32767, -32768)
open(os.path.join(d, 's.spk'), 'wb').write(bytes(spk))
