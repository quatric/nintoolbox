#!/usr/bin/env python3
# Synthetic Capcom MT Framework big-endian ARC ("\0CRA"): one stored and one zlib member, as t.arc.
import struct, sys, os, zlib
members = [(b'dir\\one', 0x241f5deb, b'\0XET' + bytes(28), False), (b'dir\\two', 0x619cf7e7, b'\0TLP' + bytes(60), True)]
hdr = b'\0CRA' + struct.pack('>HH', 7, len(members))
off = 8 + 0x50 * len(members)
table = b''
data = b''
for name, h, payload, comp in members:
    blob = zlib.compress(payload) if comp else payload
    table += name.ljust(0x40, b'\0') + struct.pack('>IIII', h, len(blob), len(payload) << 3 | 2, off + len(data))
    data += blob
open(os.path.join(sys.argv[1], 't.arc'), 'wb').write(hdr + table + data)
# Capcom wrapped BRRES: "\0XET" header (0x20 bytes) + payload starting with "bres"
open(os.path.join(sys.argv[1], 't.tex'), 'wb').write(b'\0XET' + struct.pack('>II', 0x87, 0x20) + bytes(20) + b'bres' + bytes(0x40))
# Capcom CAPS sound pack: two mono DSPW descriptors followed by their wave blocks (DSP header + frames)
def wave(frames, fill):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, frames * 14, frames * 16, 32000)
    struct.pack_into('>III', h, 0x10, 2, frames * 16 - 1, 2)
    return bytes(h) + bytes([fill]) * (frames * 8)
waves = [wave(40, 0x11), wave(20, 0x22)]
descs = b''
for w in waves:
    descs += b'DSPW' + struct.pack('>III', 1, len(w) + 0x20, 0x20) + struct.pack('>IIII', 0, 0, 1, 56)
base = 0x20 + 0x20 * len(waves)
head = b'CAPS' + struct.pack('>IIIIIII', 11, len(waves), 1, len(waves), base, base, base)
open(os.path.join(sys.argv[1], 't.caps'), 'wb').write(head.ljust(0x20, b'\0') + descs + b''.join(waves))
