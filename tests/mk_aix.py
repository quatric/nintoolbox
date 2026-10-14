#!/usr/bin/env python3
# Synthetic CRI AIX t.aix: two segments, two layers, each layer a stream of AIXP chunks.
import struct, sys, os
segs, layers = 2, 2
head = bytearray(0x800)
head[0:4] = b'AIXF'
struct.pack_into('>III', head, 4, 0x800 - 8, 0x01000014, 0x800)
struct.pack_into('>H', head, 0x18, segs)
chunks = []
def seg_data(s):
    out = b''
    for rnd in range(2):
        for l in range(layers):
            payload = bytes([0x10 * s + 0x01 * l + rnd] * 12)
            out += b'AIXP' + struct.pack('>IbbhI', 0x10 - 8 + len(payload), l, layers, len(payload), 0xffffffff) + payload
    return out + b'AIXE' + struct.pack('>I', 8)
datas = [seg_data(s) for s in range(segs)]
off = 0x800
for s in range(segs):
    struct.pack_into('>IIII', head, 0x20 + 0x10 * s, off, len(datas[s]), 1000, 32000)
    off += len(datas[s])
sub = 0x20 + 0x10 * segs
head[sub] = 1
lay = sub + 0x10
head[lay] = layers
for l in range(layers):
    struct.pack_into('>II', head, lay + 8 + 8 * l, 32000, 2)
open(os.path.join(sys.argv[1], 't.aix'), 'wb').write(bytes(head) + b''.join(datas))
