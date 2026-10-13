#!/usr/bin/env python3
# Synthetic Epic Mickey .pak: a stored member "Dir/One/a.bin" and a zlib member "Other/b.txt".
import struct, sys, os, zlib
d = sys.argv[1]
A = b'stored-data-0123456789'
B = b'compress me ' * 40
Bz = zlib.compress(B)
dirs = [b'Dir/One', b'Other']
files = [b'a.bin', b'b.txt']
pool = b''
offs = {}
for s in dirs + files:
    offs[s] = len(pool)
    pool += s + b'\0'
def pad(n): return (n + 31) & ~31
members = [(files[0], dirs[0], A, A), (files[1], dirs[1], B, Bz)]
table = b''
for f, dd, raw, comp in members:
    table += struct.pack('>IIIII', offs[f], len(raw), len(comp), pad(len(comp)), offs[dd]) + bytes(4)
tab_size = len(table) + 4 + len(pool)
tab_total = pad(0x20 + tab_size) - 0x20
hdr = struct.pack('>8I', 0x204b4150, 2, 0, 0x20, tab_total, 0, 0, 0)
body = table + struct.pack('>I', len(pool)) + pool
body += bytes(tab_total - len(body))
data = b''
for f, dd, raw, comp in members:
    data += comp + bytes(pad(len(comp)) - len(comp))
open(os.path.join(d, 't.pak'), 'wb').write(hdr + body + data)
