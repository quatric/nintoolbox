#!/usr/bin/env python3
"""Synthetic Ubisoft Jade BigFiles (see project/src/lib-jadebig.h): a plain
"BIG\\0" v44 file and its XOR-obfuscated "BUG\\0" twin with a stripped name.
usage: mk_jadebig.py OUTDIR -> OUTDIR/TEST.bf, OUTDIR/TESTX.bf"""
import os, struct, sys

KEY = bytes([0xb3, 0x98, 0xcc, 0x66])
SOF = 8  # size_of_fat
INFO = 0x7C
DIRS = [(-1, 'ROOT'), (0, 'Bin'), (1, 'Sub dir')]
FILES = [  # (dir, name, key, payload)
    (1, 'one.bin', 0x0e000001, b'hello jade\n'),
    (2, 'two.bin', 0x0e000002, b'second member'),
    (1, '', 0xfc000003, b'unnamed'),
]


def nm(s, n=0x40):
    return s.encode().ljust(n, b'\0')


def build(magic):
    hdr = struct.pack('<9I', 44, len(FILES), len(DIRS), 0, 0, 0xffffffff, 0xffffffff, SOF, 1)
    fat_base = 0x2c + 44
    fat_len = 0x18 + SOF * (8 + INFO + 0x54)
    pos = fat_base + 0x18
    data_off = fat_base + fat_len
    refs = b''
    infos = b''
    payload = b''
    for d, n, k, p in FILES:
        off = data_off + len(payload)
        refs += struct.pack('<II', off, k)
        info = struct.pack('<IiiiI', len(p), -1, -1, d, 0) + nm(n) + struct.pack('<I', 0) + b'\0' * 0x24
        assert len(info) == INFO
        infos += info
        payload += struct.pack('<I', len(p)) + p
    refs = refs.ljust(SOF * 8, b'\0')
    infos = infos.ljust(SOF * INFO, b'\0')
    dirs = b''
    for par, n in DIRS:
        dirs += struct.pack('<5i', -1, -1, -1, -1, par) + nm(n)
    dirs = dirs.ljust(SOF * 0x54, b'\0')
    fat = struct.pack('<IIIiII', len(FILES), len(DIRS), pos, -1, 0, SOF - 1) + refs + infos + dirs
    out = bytearray(magic + hdr + struct.pack('<I', 0x0e00adc0) + b'\0' * 44 + fat)
    if magic == b'BUG\0':
        for i in range(4, 0x28):
            out[i] ^= KEY[i & 3]
        for i in range(fat_base, fat_base + len(fat)):
            if out[i] or True:
                out[i] ^= KEY[i & 3]
    return bytes(out) + payload


os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.bf'), 'wb').write(build(b'BIG\0'))
open(os.path.join(sys.argv[1], 'TESTX.bf'), 'wb').write(build(b'BUG\0'))
