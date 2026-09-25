#!/usr/bin/env python3
"""Synthetic Bomberman Land cddata*.dig streaming resource package (see
project/src/lib-dig.h): a 12-byte header, a flat entry table filling the
first 0x800 bytes (offset_sectors/size_sectors/unknown/reserved, sector =
2048 bytes), and two sector-aligned data blobs. usage:
mk_dig.py OUTDIR -> OUTDIR/test.dig"""
import os, struct, sys

SECTOR = 0x800  # 2048

a = b'hello dig entry one\n'
b = b'second dig entry payload\n'

# Two entries, sector 1 and sector 2 (right after the one-sector table).
entries = [
	(1, 1, 0, 0),  # offset_sectors=1 (0x800), size_sectors=1 (covers "a", padded)
	(2, 1, 0, 0),  # offset_sectors=2 (0x1000), size_sectors=1 (covers "b", padded)
]

header = struct.pack('>III', 1, 0x1234, len(entries))
table = bytearray(SECTOR)
table[0:len(header)] = header
pos = 16
for off_sect, size_sect, unk, rsv in entries:
	struct.pack_into('>IIII', table, pos, off_sect, size_sect, unk, rsv)
	pos += 16

data0 = a.ljust(SECTOR, b'\0')
data1 = b.ljust(SECTOR, b'\0')

out = bytes(table) + data0 + data1

os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.dig'), 'wb').write(out)
