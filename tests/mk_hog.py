#!/usr/bin/env python3
# Synthetic HOG archive with two members, written as t.hog.
import struct, sys, os
out = sys.argv[1]
files = [('DIR\\A.TXT', b'hello hog'), ('B.BIN', b'\x01\x02\x03\x04')]
names = b''.join(n.encode() + b'\0' for n, _ in files)
base = 0x20 + 16 * len(files)
noff = base
doff = base + len(names)
tab = b''
for n, d in files:
    tab += struct.pack('<IIII', noff, doff, len(d), 0)
    noff += len(n) + 1
    doff += len(d)
hdr = struct.pack('<HH7I', 1, 2, 0x20, 0, 0, len(files), len(names), 0, 0)
open(os.path.join(out, 't.hog'), 'wb').write(hdr + tab + names + b''.join(d for _, d in files))

# EA SHOC-style stream: CTRL, then SHDR + Zdat (split zlib stream) + FILL
import zlib
def chunk(tag, body): return tag + struct.pack('>I', 8 + len(body)) + body
def shoc(tag, body): return chunk(b'SHOC', b'\0' * 8 + tag + body)
payload = b'shoc payload ' * 20
z = zlib.compress(payload)
h = len(z) // 2
s = chunk(b'CTRL', b'\0' * 16)
s += shoc(b'SHDR', struct.pack('>I4sII', 1, b'sfx ', 7, len(payload)) + b'\0' * 4)
s += shoc(b'Zdat', struct.pack('>I', h) + z[:h])
s += chunk(b'FILL', b'\0' * 12)
s += shoc(b'Zdat', struct.pack('>I', len(z) - h) + z[h:])
open(os.path.join(out, 's.hog'), 'wb').write(s)
open(os.path.join(out, 'g.gcb'), 'wb').write(s)
