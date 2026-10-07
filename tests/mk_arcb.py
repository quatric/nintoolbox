#!/usr/bin/env python3
"""Synthetic Sega ARCB archive (see project/src/lib-arcb.h): a U8 tree over one
AVLZ block, with a directory, a nested file and the bit-31 'packed' offset flag.
usage: mk_arcb.py OUTDIR -> OUTDIR/TEST.arc"""
import os, struct, sys


def avlz(data):
    """Literal-only AVLZ stream."""
    out = bytearray()
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        out.append((1 << len(chunk)) - 1)
        out += chunk
    return b'AVLZ' + struct.pack('>II', len(data), len(out) + 12) + bytes(out)


files = [('one.txt', b'inside the directory\n', 1), ('two.bin', bytes(range(40)), 0)]
blob = b''
offs = []
for _, d, _ in files:
    offs.append(len(blob))
    blob += d
names = b'\0' + b''.join(n.encode() + b'\0' for n in ['dir', 'one.txt', 'two.bin'])
nname = {'dir': 1, 'one.txt': 5, 'two.bin': 13}
nodes = struct.pack('>III', 0x01000000, 0, 4)
nodes += struct.pack('>III', 0x01000000 | nname['dir'], 0, 3)
nodes += struct.pack('>III', nname['one.txt'], offs[0] | 0x80000000, len(files[0][1]))
nodes += struct.pack('>III', nname['two.bin'], offs[1], len(files[1][1]))
hsize = len(nodes) + len(names)
data_ofs = (0x20 + hsize + 0x7f) & ~0x7f
u8 = b'U\xaa8-' + struct.pack('>III', 0x20, hsize, data_ofs) + b'\0' * 16 + nodes + names
u8 = u8.ljust(data_ofs, b'\0')
packed = avlz(blob)
total = 0x20 + data_ofs + len(packed)
hdr = b'ARCB' + struct.pack('>7I', 0x03010000, 0x00010000, total, 0x20 + data_ofs, len(packed), total, 0)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.arc'), 'wb').write(hdr + u8 + packed)
