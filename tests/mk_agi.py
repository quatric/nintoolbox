#!/usr/bin/env python3
"""Synthetic Toys for Bob AGI archive (see project/src/lib-agi.h): two
members, "sounds\\a.txt" (plain) and "models\\b.igz" (plain). usage:
mk_agi.py OUTDIR -> OUTDIR/test.pak"""
import os, struct, sys

a = b'hello agi\n'
b = b'member two\n'

HDR = 0x40
# Entry table: two adjacent (offset, size) big-endian u32 word pairs.
entry_table_size = 16
data0_off = HDR + entry_table_size
data1_off = data0_off + len(a)
name_table_offset = data1_off + len(b)

names = [b'sounds\\a.txt', b'models\\b.igz']
off_table_size = 4 * len(names)  # offsets are relative to the name table start
name_off_table = struct.pack('>2I', off_table_size, off_table_size + len(names[0]) + 1 + 4)
name_bytes = b''.join(n + b'\0' + b'\0\0\0\0' for n in names)  # NUL + 4-byte hash
name_table = name_off_table + name_bytes
name_table_size = len(name_table)

hdr = struct.pack('>IIIIIIII', 0x1A414749, 9, entry_table_size, len(names), 0x800, 0, 0, 0)
hdr = hdr.ljust(0x2c, b'\0')
hdr += struct.pack('>IIIII', name_table_offset, name_table_size, 3, 0, 0)
hdr = hdr.ljust(HDR, b'\0')

entry_table = struct.pack('>4I', data0_off, len(a), data1_off, len(b))

out = hdr + entry_table + a + b + name_table
assert len(out) == name_table_offset + name_table_size

os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.pak'), 'wb').write(out)
