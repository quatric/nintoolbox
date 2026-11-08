#!/usr/bin/env python3
# Synthetic DLS bank with two waves (one with an INFO/INAM name): t.dls
import struct, sys, os
def ck(t, d):
    return t + struct.pack('<I', len(d)) + d + (b'\0' if len(d) & 1 else b'')
def lst(t, d):
    return ck(b'LIST', t + d)
fmt = struct.pack('<HHIIHH', 1, 1, 22050, 44100, 2, 16)
def wave(samples, name=None):
    body = ck(b'fmt ', fmt) + ck(b'data', struct.pack('<%dh' % len(samples), *samples))
    if name:
        body += lst(b'INFO', ck(b'INAM', name + b'\0'))
    return lst(b'wave', body)
pool = lst(b'wvpl', wave([0, 1000, -1000, 0], b'PIANO') + wave([5, 6, 7]))
dls = b'DLS ' + ck(b'colh', struct.pack('<I', 0)) + pool
open(os.path.join(sys.argv[1], 't.dls'), 'wb').write(b'RIFF' + struct.pack('<I', len(dls)) + dls)
