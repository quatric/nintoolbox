#!/usr/bin/env python3
# Synthetic EA "SCHl" stream with codec2 0x17 (EALayer3): two mono MPEG-1 granules in one SCDl block (see
# extract/ealayer3.inc). Expected output: one 1044-byte 320 kbit Layer III frame, header ff fb e0.
import struct, sys, os
out = sys.argv[1]
class BW:
    def __init__(s): s.v = 0; s.n = 0
    def put(s, n, v): s.v = (s.v << n) | (v & ((1 << n) - 1)); s.n += n
    def bytes(s):
        pad = -s.n % 8; return (s.v << pad).to_bytes((s.n + pad) // 8, 'big')
def granule(gi):
    w = BW(); w.put(8, 0)                       # not a PCM block
    w.put(2, 3); w.put(2, 0); w.put(2, 3); w.put(2, 0)   # MPEG1, 44100, mono, no joint stereo
    w.put(1, gi)
    if gi: w.put(4, 0)                          # scfsi
    w.put(12, 0); w.put(32, 0); w.put(15, 0)    # no main data
    return w.bytes()
patch = (b'\x82\x01\x01' + b'\x84\x03' + (44100).to_bytes(3, 'big') + b'\xa0\x01\x17' + b'\x85\x03\x00\x04\x80' + b'\xff')
hdr = b'SCHl' + struct.pack('<I', 8 + 4 + len(patch)) + b'PT' + struct.pack('<H', 6) + patch
data = granule(0) + granule(1)
blk = b'SCDl' + struct.pack('<I', 16 + len(data)) + struct.pack('>II', 1152, 0) + data
f = hdr + blk + b'SCEl' + struct.pack('<I', 8)
open(os.path.join(out, 't.ast'), 'wb').write(f)
