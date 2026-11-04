#!/usr/bin/env python3
# Synthetic FBND bundle: zeroed table, a BCV0 member at 0x1000 and a one-file U8 archive at 0x1800.
import struct, sys, os
out = sys.argv[1]
payload = b'fbnd u8 member'
names = b'\0file.txt\0'
nodes = struct.pack('>BBHII', 1, 0, 0, 0, 2) + struct.pack('>BBHII', 0, 0, 1, 0, 0)
hdr_total = 0x20 + len(nodes) + len(names)
data_off = (hdr_total + 0x3f) & ~0x3f
nodes = struct.pack('>BBHII', 1, 0, 0, 0, 2) + struct.pack('>BBHII', 0, 0, 1, data_off, len(payload))
u8 = b'U\xaa8-' + struct.pack('>III', 0x20, len(nodes) + len(names), data_off) + b'\0' * 16
u8 = u8 + nodes + names
u8 = u8.ljust(data_off, b'\0') + payload
bcv = b'BCV0' + struct.pack('>III', 0x10, 1, 0) + b'EOCK' + b'\0' * 12 + b'raw payload'
f = bytearray(0x1800 + len(u8))
f[0:8] = b'FBND' + struct.pack('>I', 0x630)
f[0x1000:0x1000 + len(bcv)] = bcv
f[0x1800:0x1800 + len(u8)] = u8
open(os.path.join(out, 't.bin'), 'wb').write(bytes(f))
