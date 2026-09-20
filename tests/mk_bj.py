#!/usr/bin/env python3
"""Synthetic "Bj" engine assets (see project/src/lib-bj.h): a .tx1/.tx2 pair,
a .mtm mesh tree with one textured triangle, a .bsi sound bank and a .bsm
stream. usage: mk_bj.py OUTDIR -> OUTDIR/{8_16/T.tx1,8_16/T.tx2,m1/M.mtm,S.bsi,S.bsm}"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out + '/8_16', exist_ok=True); os.makedirs(out + '/m1', exist_ok=True)
L = lambda *a: struct.pack('<%dI' % len(a), *a)

# texture: 16x16 paletted, no mips
pal = b''.join(bytes([i, 255 - i, 0, 255]) for i in range(4))
rec = 0x844
tx1 = bytearray(0x850 + 0x20 + len(pal))
tx1[0:16] = L(0xfacc00ff, 1, rec, 0)
tx1[rec:rec + 16] = L(0, 8, 0, 0x00010000)  # 1 unit of 2 KiB at start 0
struct.pack_into('<BBHHHHHI', tx1, rec + 16, 12, 1, 0, 0x9000, 0, 16, 16, 4)
tx1[rec + 0x20:rec + 0x20 + len(pal)] = pal
open(out + '/8_16/T.tx1', 'wb').write(tx1[:rec + 0x20 + len(pal)])
open(out + '/8_16/T.tx2', 'wb').write(bytes(i % 4 for i in range(256)) + bytes(2048 - 256))

# mesh tree: root node (identity) with one mesh: 3 positions, 3 vertices with UV, one list
d = bytearray(0x400)
struct.pack_into('<7I', d, 0, 0x80178e55, 1, 0, 1, 0x30, 0x40, 0)
d[0x30:0x34] = L(0)
node, mesh, mp = 0x40, 0x100, 0x1f0
struct.pack_into('<I', d, node + 108, 1); struct.pack_into('<I', d, node + 176, mp)
struct.pack_into('<16f', d, node + 112, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
d[mp:mp + 4] = L(mesh)
P, V, I, R = 0x200, 0x240, 0x290, 0x2b0
struct.pack_into('<I', d, mesh + 8, 1)  # format 1: 12-byte vertices with UV
struct.pack_into('<I', d, mesh + 64, 0)
struct.pack_into('<4I', d, mesh + 76, 3, 0, 0, 3)
struct.pack_into('<I', d, mesh + 92, 3); struct.pack_into('<I', d, mesh + 104, 1)
struct.pack_into('<I', d, mesh + 116, P); struct.pack_into('<I', d, mesh + 128, V)
struct.pack_into('<I', d, mesh + 132, I); struct.pack_into('<I', d, mesh + 144, R)
for i, p in enumerate([(0, 0, 0), (1, 0, 0), (0, 1, 0)]):
    struct.pack_into('<4f', d, P + 16 * i, *p, 0)
    struct.pack_into('<HHff', d, V + 12 * i, i, 0, p[0], p[1])
    struct.pack_into('<H', d, I + 2 * i, i)
struct.pack_into('<6H', d, R, 4 | 8, 0, 0, 0, 3, 0)  # indexed triangle list
open(out + '/m1/M.mtm', 'wb').write(d)

# sound bank: 1 sample, 8 bytes at 11025 Hz
bsi = struct.pack('>4I', 0x5002d, 1, 0x10, 0) + struct.pack('>8I', 0x01000000, 0, 11025, 8, 0, 0x30, 0, 0) + bytes([0, 10, 20, 30, 0xf0, 0xe0, 0xd0, 0xc0])
open(out + '/S.bsi', 'wb').write(bsi)
open(out + '/S.bsm', 'wb').write(bytes(0x1000) * 2)
