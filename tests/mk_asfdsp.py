#!/usr/bin/env python3
# Synthetic multi-stream DSP container t.asf (Brunswick Pro Bowling layout): two mono streams of 16 / 8 bytes.
import struct, sys, os
def dsphdr(nbytes, rate):
    nib = nbytes * 2
    h = bytearray(0x60)
    struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, rate, 0, 0, 2, nib - 1, 2)
    for i in range(16):
        struct.pack_into('>h', h, 0x1c + 2 * i, 0x100 * (i + 1))
    return bytes(h)
# third stream: stereo (flag 0x0202), the short tail is split in halves (header describes one 8-byte channel)
streams = [(bytes(range(1, 17)), 44100), (bytes(range(0x21, 0x29)), 22050), (bytes(range(0x31, 0x39)) + bytes(range(0x41, 0x49)), 32000)]
start = 0x0c + 0x70 * 3
offs = [start]
for d, r in streams[:-1]:
    offs.append((offs[-1] + len(d) + 0x1f) & ~0x1f)
recs = b''
for i, (d, r) in enumerate(streams):
    nxt = struct.pack('<II', offs[i + 1], len(streams[i + 1][0])) if i + 1 < len(streams) else bytes(8)
    recs += struct.pack('<I', 0x0202 if i == 2 else 0x0201) + b'T_%02d' % i + dsphdr(8 if i == 2 else len(d), r) + nxt
body = struct.pack('<III', len(streams), start, len(streams[0][0])) + recs
body = body.ljust(offs[-1], b'\0')
for i, (d, r) in enumerate(streams):
    body = body[:offs[i]] + d + body[offs[i] + len(d):]
    body = body.ljust(offs[i] + len(d), b'\0')
open(os.path.join(sys.argv[1], 't.asf'), 'wb').write(body)
