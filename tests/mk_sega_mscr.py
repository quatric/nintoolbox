#!/usr/bin/env python3
"""Synthetic compressed SEGA MSCR script (Sakura Wars .msX; see project/src/wszst_cmd/extract/sega_mscr.inc): inverted-byte LZ with
literals, a short match and a long match, then the end marker. Body = 'ABCDEFGH' + 'ABC' (short match, offset -8) + 'ABCDEFGHA' (long match, offset -11).
usage: mk_sega_mscr.py OUT.msX"""
import struct, sys
# flag bits go into inverted flag bytes interleaved with the data
ops = [('l', c) for c in b'ABCDEFGH'] + [('s', 3, -8), ('L', 9, -11), ('e',)]
stream = bytearray(); flagpos = None; nb = 0
def flag(bit):
    global flagpos, nb
    if nb == 0:
        flagpos = len(stream); stream.append(0); nb = 8
    if not bit:  # stored flag byte is inverted
        stream[flagpos] |= 1 << (8 - nb)
    nb -= 1
for o in ops:
    if o[0] == 'l': flag(1); stream.append(~o[1] & 0xff)
    elif o[0] == 's':
        flag(0); flag(0); flag((o[1] - 2) >> 1 & 1); flag((o[1] - 2) & 1); stream.append(~(o[2] + 0x100) & 0xff)
    elif o[0] == 'L':
        flag(0); flag(1); v = o[2] + 0x2000; b1 = v >> 5; b0 = ((v & 31) << 3) | (o[1] - 2)
        stream.extend([~b0 & 0xff, ~b1 & 0xff])
    else:
        flag(0); flag(1); stream.extend([0xff, 0xff])
body = bytes(stream)
hdr = bytearray(0x30)
hdr[0:4] = b'MSCR'; struct.pack_into('<IIII', hdr, 4, len(body), 0x20, 0x400, 0)
struct.pack_into('<II', hdr, 0x20, 0x16, 0x8000); hdr[0x28:0x30] = b'\xff' * 8
open(sys.argv[1], 'wb').write(bytes(hdr) + body)
