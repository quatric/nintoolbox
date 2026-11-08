#!/usr/bin/env python3
# Synthetic classic Telltale .ttarch (version 4, little-endian): m1.ttarch = stored data (files_mode 1),
# m2.ttarch = zlib-wrapped 64 KiB chunks (files_mode 2).
import struct, sys, os, zlib
out = sys.argv[1]
files = [('a.anm', b'ERTM' + bytes(range(200))), ('b.d3dtx', b'ERTM' + b'\x07' * 70000), ('c.lua', b'print(1)\n' * 10)]

def table(offs):
    t = struct.pack('<I', 0)  # no folders
    t += struct.pack('<I', len(files))
    for (n, d), o in zip(files, offs):
        t += struct.pack('<I', len(n)) + n.encode() + struct.pack('<III', 0, o, len(d))
    return t

offs = []; pos = 0
for n, d in files:
    offs.append(pos); pos += len(d)
blob = b''.join(d for _, d in files)
t = table(offs)

# files_mode 1: stored
hdr = struct.pack('<5I', 4, 0, 3, 1, 0) + struct.pack('<I', len(blob)) + struct.pack('<2I', 0, 0) + struct.pack('<I', len(t))
open(os.path.join(out, 'm1.ttarch'), 'wb').write(hdr + t + blob)

# files_mode 2: chunks of 0x10000 (last one short), each zlib wrapped
chunks = []
for i in range(0, len(blob), 0x10000):
    c = blob[i:i + 0x10000]
    chunks.append(zlib.compress(c))
hdr = struct.pack('<5I', 4, 0, 3, 2, len(chunks)) + b''.join(struct.pack('<I', len(c)) for c in chunks)
hdr += struct.pack('<I', sum(len(c) for c in chunks)) + struct.pack('<2I', 0, 0) + struct.pack('<I', len(t))
open(os.path.join(out, 'm2.ttarch'), 'wb').write(hdr + t + b''.join(chunks))
open(os.path.join(out, 'b.d3dtx'), 'wb').write(files[1][1])
