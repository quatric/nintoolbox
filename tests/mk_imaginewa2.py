#!/usr/bin/env python3
"""Synthetic Imagine .wa2: 2 bones (root -> child), 1 clip of 2 frames. usage: mk_imaginewa2.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
nb, nc = 2, 1
head = 8 + 4 * (nb + nc)
bone0 = struct.pack('>I3fII', 0, 1.0, 2.0, 3.0, 1, 0) + struct.pack('>I', 1)
bone1 = struct.pack('>I3fII', 1, 0.5, 0.0, 0.0, 0, 0)
o0 = head
o1 = o0 + len(bone0)
oc = o1 + len(bone1)
N = 2
clip = struct.pack('>II', N, 0) + bytes(16)
clip += struct.pack('>II', 7, 9)                                  # frame flags
clip += struct.pack('>6f', 0.1, 0, 0, 0.2, 0, 0)                  # root
clip += struct.pack('>6f', 10, 20, 30, 11, 21, 31)                # second track
for f in range(N):
    for b in range(nb):
        clip += struct.pack('>4f', 0, 0, 0, 1)                    # identity quaternions
open(os.path.join(d, 't.wa2'), 'wb').write(struct.pack('>II', nc, nb) + struct.pack('>2I', o0, o1) + struct.pack('>I', oc) + bone0 + bone1 + clip)
