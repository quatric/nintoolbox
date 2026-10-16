#!/usr/bin/env python3
# Synthetic Ubisoft DARE package t3.pk (Avatar layout, 0x220015): one stereo 32 kHz Ubisoft IMA stream of 14 frames in the
# external t3.spk (version 5 header, 10 PCM frames, 4 nibble bytes). Writes the expected PCM from an independent IMA
# implementation to t3.expect (little-endian s16, interleaved).
import struct, sys, os
d = sys.argv[1]
STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
         118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
         1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
         6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8] * 2
pcm = [(100 * i, -50 * i) for i in range(10)]
hist = [pcm[-1][0], pcm[-1][1]]
step = [10, 12]
# version 5: byte 0 = version, byte 1 = pad, header (offset 2) = 0x0c bytes unknown, u16 header samples, then per channel
# s16 history + s8 step index + pad, 0x08 more bytes of header and 4 null bytes (version >= 3)
data = bytearray(2 + 0x16 + 4)
data[0] = 5
struct.pack_into('<H', data, 2 + 0x0c, 10)
struct.pack_into('<hbxhbx', data, 2 + 0x0e, hist[0], step[0], hist[1], step[1])
for l, r in pcm:
    data += struct.pack('<hh', l, r)
codes = [(1, 9), (5, 3), (0, 15), (7, 8)]
exp = list(pcm)
for cl, cr in codes:
    data.append(cl << 4 | cr)
    out = []
    for c, code in ((0, cl), (1, cr)):
        delta = ((code & 7) * 2 + 1) * STEPS[step[c]] >> 3
        if code & 8:
            delta = -delta
        hist[c] = max(-32768, min(32767, hist[c] + delta))
        step[c] = max(0, min(88, step[c] + IDX[code]))
        out.append(hist[c])
    exp.append(tuple(out))
hdr = bytearray(0xa8 + 0x80)
struct.pack_into('>I', hdr, 0, 0x01220015)
struct.pack_into('<I', hdr, 4, 0x28)
struct.pack_into('>I', hdr, 0x20, 0x20000000)
h = 0x28
for off, val in ((0, 0x3001), (4, 1), (8, len(data)), (0x1c, 0x3002), (0x28, 1), (0x34, 1), (0x44, 2), (0x4c, 32000),
                 (0x54, 0), (0x5c, 14), (0x64, 3), (0x68, 0), (0x6c, 0), (0x74, 0)):
    struct.pack_into('>I', hdr, h + off, val)
index = struct.pack('<II', 0x3001, len(hdr))
strs = b't3.spk\0'
res = struct.pack('<II', 1, len(strs)) + strs + struct.pack('<IIII', 0x3002, 0, 0, 0x28 + len(data))
head = bytearray(0x40)
head[0:4] = struct.pack('>I', 0x01220015)
struct.pack_into('<II', head, 4, len(index), 0x40 + len(index) + len(hdr))
open(os.path.join(d, 't3.pk'), 'wb').write(bytes(head) + index + bytes(hdr) + res)
open(os.path.join(d, 't3.spk'), 'wb').write(bytes(0x28) + bytes(data))
open(os.path.join(d, 't3.expect'), 'wb').write(b''.join(struct.pack('<hh', l, r) for l, r in exp))
