#!/usr/bin/env python3
"""Synthetic 2XL Games LPAK (see project/src/lib-lpak.h): a one-block and a
two-block (zlib stream per 0x4000 bytes) member. usage: mk_lpak.py OUTDIR -> OUTDIR/TEST.PAK"""
import os, struct, sys, zlib

small = b'hello lpak\n'
big = bytes((i * 7) & 0xff for i in range(0x4000)) + b'tail of the second block'


def chunk(tag, body):
    return tag + struct.pack('<I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def member(parts, data):
    blocks = [zlib.compress(data[i:i + 0x4000]) for i in range(0, len(data), 0x4000)]
    starts, o = [], 0
    for b in blocks[:-1]:
        o += len(b)
        starts.append(o)
    hdr = 12 + 4 * len(starts)
    f = struct.pack('<3I', 0x12345678, len(data), hdr) + b''.join(struct.pack('<I', s) for s in starts)
    f += b''.join(blocks)
    strs = b''.join(p.encode() + b'\0' for p in parts) + b'\0'
    body = b'LDAT' + chunk(b'dir ', b'\0' * 12) + chunk(b'file', f) + chunk(b'str ', strs)
    return chunk(b'LIST', body)


body = b'LPAK' + member(['Dir', 'wii', 'a.txt'], small) + member(['Dir', 'big.bin'], big)
riff = chunk(b'RIFF', body)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.PAK'), 'wb').write(struct.pack('<I', len(riff) - 8) + riff)
