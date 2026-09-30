#!/usr/bin/env python3
"""Synthetic NIBM .aud (see project/src/lib-nibm.h): a serialized-class header
followed by a structurally valid Ogg stream (four pages, arbitrary payloads).
usage: mk_nibm.py OUTDIR -> OUTDIR/T.aud and OUTDIR/expected.ogg"""
import os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def page(seq, payload, flags=0):
    segs = []
    n = len(payload)
    while n >= 255:
        segs.append(255); n -= 255
    segs.append(n)
    hdr = b'OggS' + struct.pack('<BBqIIIB', 0, flags, 0, 0xe858, seq, 0, len(segs)) + bytes(segs)
    return hdr + payload


ogg = page(0, b'\x01vorbis' + bytes(23), 2) + page(1, b'\x03vorbis' + bytes(40)) \
    + page(2, b'\x05vorbis' + bytes(300)) + page(3, bytes(range(200)) * 3)
head = b'NIBM' + struct.pack('<II', 2, 15) + b'class AudioData' + bytes(8) + b'struct AudioData::Streamed'
head = head.ljust(126, b'\0')
open(os.path.join(out, 'T.aud'), 'wb').write(head + ogg)
open(os.path.join(out, 'expected.ogg'), 'wb').write(ogg)
