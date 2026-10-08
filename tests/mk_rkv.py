#!/usr/bin/env python3
# Synthetic RK33 .rkv with one raw member and one LZF member.
import struct, sys, os
out = sys.argv[1]
raw = b'raw rkv member'
txt = b'abcabcabcabcabc'
# LZF: literal run "abc" (ctrl 2) then back-reference len 12 at distance 3
lzf = bytes([2]) + b'abc' + bytes([7 << 5, 12 - 9, 2])  # ctrl: len 7+3, dist 3
comp = b'\x02' + lzf
off1 = 0x100
off2 = off1 + len(raw)
body = raw + comp
toc_off = 0x100 + len(body)
def ent(name, off, size, cs):
    return name.ljust(0x40, b'\0') + struct.pack('>QQIIII', 0, off, 0, size, cs, 0)
toc = ent(b'a.txt', off1, len(raw), len(raw)) + ent(b'b.txt', off2, 15, len(comp))
hdr = b'test.rkv'.ljust(0x40, b'\0') + struct.pack('>QIIII4s', toc_off, 6, 2, len(toc), 0, b'RK33')
hdr = hdr[:0x54] + b'RK33' + hdr[0x58:]
hdr = hdr.ljust(0x100, b'\0')
open(os.path.join(out, 't.rkv'), 'wb').write(hdr + body + toc)
