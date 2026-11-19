#!/usr/bin/env python3
"""Synthetic Toshi terrain locator list (Nickelodeon Barnyard Wii, Data/Terrain/<area>/*skel.trb; see
project/src/wszst_cmd/extract/toshi_loc.inc): "Locators" {count, ptr, name count, ptr names} with two 32-byte locators
{quaternion, position, u16 name id, u8 flags1 (scale nibble 5 = 1.0, flag nibble 0xf), s8 flags2} and a one-section "LocatorVIS".
usage: mk_toshi_loc.py OUTDIR  ->  OUTDIR/T_skel.trb"""
import os, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)
F = lambda *a: struct.pack('>%df' % len(a), *a)
S = bytearray(0x200)
def put(off, b): S[off:off + len(b)] = b
LOC, VIS, LPTR, NPTR, NAMES, VSEC, STR = 0x00, 0x10, 0x20, 0x80, 0x90, 0xa0, 0xc0
put(LOC, B(2, LPTR, 2, NPTR))
put(LPTR, F(0, 0, 0, 1) + F(1.5, 2.5, 3.5) + struct.pack('>HBb', 0, 0xf5, 127))
put(LPTR + 32, F(0, 0.7071068, 0, 0.7071068) + F(-4, 5, 6) + struct.pack('>HBb', 1, 0x15, 0))
put(NPTR, B(STR, STR + 8))
put(STR, b'tree_a\0')
put(STR + 8, b'rock_b\0')
put(VIS, B(1, VSEC))
put(VSEC, B(STR + 16, 0, 2))
put(STR + 16, b'all\0')
names = [(b'Locators', LOC), (b'LocatorVIS', VIS)]
symb = B(len(names)); nm = b''; ents = b''
for n, off in names:
    ents += struct.pack('>HHII', 0, len(nm), 0, off); nm += n + b'\0'
symb += ents + nm
def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)
body = b'FBRT' + hunk(b'XRDH', bytes(24)) + hunk(b'TCES', bytes(S)) + hunk(b'BMYS', symb)
open(out + '/T_skel.trb', 'wb').write(b'TSFB' + B(len(body)) + body + bytes(1024))
