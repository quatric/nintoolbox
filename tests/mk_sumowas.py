#!/usr/bin/env python3
"""Synthetic Sumo Digital .was stream (see project/src/lib-sumowas.h): one
mono DSP-ADPCM channel, one 0x8000-byte block, exercising the container
plumbing (global header + one lib-dsp-identical sub-header + a single block).
usage: mk_sumowas.py OUTDIR -> OUTDIR/T.was"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

BLOCK = 0x8000
NSAMPLES = 14  # exactly one ADPCM frame
SRATE = 24000

coefs = [1785, -604, 3602, -1888, 3008, -1403, 3610, -1743,
         2425, -840, 3815, -1988, 3091, -1238, 3682, -1725]

sub = bytearray(0x60)
struct.pack_into('>I', sub, 0, NSAMPLES)
struct.pack_into('>I', sub, 4, 16)  # nibble_count (frame header nibble + 14 sample nibbles + pad)
struct.pack_into('>I', sub, 8, SRATE)
struct.pack_into('>H', sub, 12, 0)  # loop_flag
struct.pack_into('>H', sub, 14, 0)  # fmt (0 = ADPCM)
for i, c in enumerate(coefs):
    struct.pack_into('>h', sub, 0x1c + 2 * i, c)
struct.pack_into('>h', sub, 0x40, 0)  # hist1
struct.pack_into('>h', sub, 0x42, 0)  # hist2

frame = bytes([0x11]) + bytes(((i * 5 + 3) & 0xff) for i in range(7))  # predictor/scale 1, 14 nibbles
block = frame + bytes(BLOCK - len(frame))

header = bytearray(0x20)
header[0:4] = b'iSWS'
struct.pack_into('>I', header, 0x04, 0x18)
struct.pack_into('>I', header, 0x08, 1)      # channels
struct.pack_into('>I', header, 0x0c, 0)      # block_count - 1
struct.pack_into('>I', header, 0x10, BLOCK)
struct.pack_into('>I', header, 0x14, len(frame))

with open(os.path.join(out, 'T.was'), 'wb') as f:
    f.write(bytes(header) + bytes(sub) + block)
