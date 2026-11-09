#!/usr/bin/env python3
"""Synthetic Shrek APKF: IMG + DATA pools, one TEX group with one 8x8 CMPR texture. usage: mk_apkf.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
pix = bytes([0xf8, 0x00, 0xf8, 0x00, 0, 0, 0, 0]) * 4                   # CMPR 8x8 = four 4x4 blocks (solid red)
tex = struct.pack('>4sIIIHHBBBB', b'GCNT', 3, 0x00400000, 32, 8, 8, 14, 1, 0, 0).ljust(0x40, b'\0') + pix
img = bytes(0xc0)
n = 2
PT = 0x58
b = bytearray(PT)
b[0:4] = b'APKF'; b[4:6] = bytes([5, 1])
struct.pack_into('>IIII', b, 8, 0x04000000, 0xffffffff, n, PT - 0x14)
struct.pack_into('>I', b, 0x1c, 0)
b[0x1c:0x20] = b'\0XET'
struct.pack_into('>IIIIIII', b, 0x20, 3, 2, 0x14, 1, 4, 0x01000020, 0)     # ver, item words, item off, count, pool words
struct.pack_into('>III', b, 0x3c, 0x10, len(img), len(tex))                # item: first word, size in IMG, size in DATA
# pool entries: tag, flags, w, w, size, offset (relative to the offset field)
img_at = PT + 0x30 + 0x20
data_at = img_at + len(img)
e0, e1 = PT, PT + 0x18
b += bytes(0x30)
b[e0:e0 + 4] = b'GMI\0'[::-1][::-1]; b[e0:e0 + 4] = b'\0GMI'
struct.pack_into('>IIIII', b, e0 + 4, 0, 0, 0, len(img), img_at - (e0 + 0x14))
b[e1:e1 + 4] = b'ATAD'
struct.pack_into('>IIIII', b, e1 + 4, 0, 0, 0, len(tex), data_at - (e1 + 0x14))
b = b.ljust(img_at, b'\0') + img + tex
open(os.path.join(d, 't.apk'), 'wb').write(bytes(b))
