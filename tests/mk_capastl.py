#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii animation/texture SRT table ("ASTL") wrapping BRRES as t.astl.
import struct, sys, os

d = sys.argv[1]

# Minimal valid BRRES
brres_data = bytearray(0x40)
brres_data[0:4] = b'bres'
struct.pack_into('>HHI', brres_data, 4, 0xfeff, 0, len(brres_data))
struct.pack_into('>HH', brres_data, 12, 0x10, 1)

# ASTL container: 0x160 header + brres
bres_off = 0x160
bres_sz = len(brres_data)
sz = bres_off + bres_sz

f = bytearray(sz)
f[0:4] = b'ASTL'
struct.pack_into('>HH', f, 4, 6, 1)
struct.pack_into('>II', f, 0x140, bres_sz, bres_off)
f[bres_off : bres_off + bres_sz] = brres_data

open(os.path.join(d, 't.astl'), 'wb').write(bytes(f))
