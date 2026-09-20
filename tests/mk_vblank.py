#!/usr/bin/env python3
"""Synthetic Vblank Wii packages (see project/src/lib-vblank.h): TEST.bfp (one
zlib member named by hash, one stored, one numbered slot) and TEST.bap (BPP3
with one DSP-ADPCM stream). usage: mk_vblank.py OUTDIR"""
import os, struct, sys, zlib

# Name hashes are the games' own (custom table, see lib-vblank.c); use known ones.
H_PALETTES = 0xca7aa5a3  # 'palettes.bin'
H_UNKNOWN = 0x12345678

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
plain = b'hello vblank ' * 20
comp = zlib.compress(plain)
stored = b'stored member'
slot = b'slot data\n' * 5
cnt = 2
base = 0x40 + 16 * cnt + 12 * 256
data_start = (base + 0x3f) & ~0x3f
blobs = [comp, stored, zlib.compress(slot)]
offs, p = [], data_start
for b in blobs:
    offs.append(p)
    p = (p + len(b) + 0x3f) & ~0x3f
hdr = b'BFP2' + struct.pack('<I', cnt) + struct.pack('<I', 0) * 2
hdr = hdr.ljust(0x40, b'\0')
tab = struct.pack('<4I', H_UNKNOWN, offs[0], len(plain), len(comp))
tab += struct.pack('<4I', H_PALETTES, offs[1], len(stored), len(stored))
slots = struct.pack('<3I', offs[2], len(slot), len(blobs[2])).ljust(12 * 256, b'\0')
body = (hdr + tab + slots).ljust(data_start, b'\0')
for o, b in zip(offs, blobs):
    body = body.ljust(o, b'\0') + b
open(os.path.join(out, 'TEST.bfp'), 'wb').write(body)
open(os.path.join(out, 'names.txt'), 'w').write('%08x\n' % H_UNKNOWN)

# BPP3: one sound, one DSP stream (28 samples, silent)
dsp = struct.pack('>III', 28, 32, 32000).ljust(0x1c, b'\0')
dsp += b''.join(struct.pack('>h', c) for c in [0x800] + [0] * 15)
dsp = dsp.ljust(0x60, b'\0') + bytes(16)
tab0 = 0x40
hdr_off = 0x80
meta_off = 0xc0
sd_off = hdr_off + 0x10
data_base = 0x100
f = bytearray(data_base + 0x40 + len(dsp))
f[0:4] = b'BPP3'
struct.pack_into('<I', f, 4, data_base)
struct.pack_into('<H', f, 0x10, 2)
struct.pack_into('<I', f, 0x14, tab0)
struct.pack_into('<3I', f, tab0 + 12, hdr_off, 8, meta_off)
f[hdr_off] = 1
struct.pack_into('<I', f, hdr_off + 8, sd_off)
struct.pack_into('<6I', f, sd_off, 0x20, len(dsp), 28, 0, len(dsp), 0x40)
f[meta_off:meta_off + 4] = b'\1\0\1\0'
art, ttl = b'Art\0', b'Beep\0'
f[meta_off + 4:meta_off + 4 + 1 + len(art) + 1 + len(ttl)] = bytes([len(art)]) + art + bytes([len(ttl)]) + ttl
f[data_base + 0x40:data_base + 0x40 + len(dsp)] = dsp
open(os.path.join(out, 'TEST.bap'), 'wb').write(f)
