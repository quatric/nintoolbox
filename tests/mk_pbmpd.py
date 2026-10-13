#!/usr/bin/env python3
# Synthetic Phantom Brave (Wii) battle map as t.mpd: one 0x40-byte A record, one 0x24-byte B record, a two-word
# table and a 16-byte extra record; grid cell (x=1, y=2) has plane 0 = 0x7f.
import struct, sys, os
d = sys.argv[1]
hdr = bytearray(0x40)
struct.pack_into('>HHHH', hdr, 0, 1, 0, 1, 0)
struct.pack_into('>I', hdr, 0x30, 4)
grid = bytearray(0x4000)
grid[(1 * 64 + 2) * 4] = 0x7f
a = bytes(range(0x40))
b = bytes(range(0x24))
tab = struct.pack('>HH', 0x1234, 0xabcd)
c = bytes(range(0xf0, 0x100))
open(os.path.join(d, 't.mpd'), 'wb').write(bytes(hdr) + bytes(grid) + a + b + tab + c)
