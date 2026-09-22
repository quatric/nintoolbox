#!/usr/bin/env python3
"""Synthetic Toshi TSFB texture libraries (see project/src/lib-toshi.h): A.ttl
with a plain TCES section and B.ttl with the same data as a BTEC "CCES"
(one literal run + one back-reference). usage: mk_toshi.py OUTDIR"""
import os, struct, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)

# section: TTL header, one 4x4 RGBA8 texture entry, name, pixels
name = b'Test\\tex.tga\0'
ent, nm = 12, 12 + 52
data = (nm + len(name) + 3) & ~3
sect = B(1, ent, 0) + B(0x307, nm, 4, 4, 0, data, 64, data + 64, data + 64, 0, 0, 0, 64)
sect += name + bytes(data - len(sect) - len(name)) + bytes(range(64))
symb = B(1) + struct.pack('>HHII', 0, 0, 0x45cc, 0) + b'TTL\0'

def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)

def tsfb(sec_hunk):
    body = b'FBRT' + hunk(b'XRDH', bytes(24)) + sec_hunk + hunk(b'BMYS', symb)
    return b'TSFB' + B(len(body)) + body + bytes(1024)  # trailing pad: tiny files are not probed

# BTEC: literal run of the first 40 bytes, then copy 40..end from offset 40 back... use plain literals + repeat
lit = sect[:40]; rest = len(sect) - 40
cmds = bytes([0x80 | 39]) + lit
# copy of 'rest' bytes cannot come from earlier data, so store it literally in 64-byte chunks
p = 40
while p < len(sect):
    n = min(64, len(sect) - p); cmds += bytes([0x80 | (n - 1)]) + sect[p:p + n]; p += n
btec = b'CETB' + struct.pack('>HH', 1, 2) + B(len(cmds), len(sect)) + cmds
open(out + '/A.ttl', 'wb').write(tsfb(hunk(b'TCES', sect)))
open(out + '/B.ttl', 'wb').write(tsfb(hunk(b'CCES', btec)))

# C.tkl: a "keylib" symbol (Toshi::TKeyframeLibrary::TRBHeader, see lib-toshi.h)
# with 2 translations, 1 quaternion, 0 scales.
tname = b'Test\0'
thdr_off = 0
name_off = 52
t_off = (name_off + len(tname) + 3) & ~3
q_off = t_off + 2 * 6
end = q_off + 1 * 8
import struct as _s
f32 = lambda v: _s.pack('>f', v)
thdr = (
    B(name_off)
    + f32(0.0001) + f32(0.0001) + f32(0.0001)
    + B(2, 1, 0, 6, 8, 4)
    + B(t_off, q_off, end)
)
tsect = thdr + tname + bytes(t_off - name_off - len(tname))
tsect += struct.pack('>3h', 100, -200, 300) + struct.pack('>3h', -100, 200, -300)
tsect += struct.pack('>4h', 0, 0, 0, 32767)
tsymb = B(1) + struct.pack('>HHII', 0, 0, 0x1234, thdr_off) + b'keylib\0'

def tsfb_tkl(sec_hunk, symb_pl):
    body = b'FBRT' + hunk(b'XRDH', bytes(24)) + sec_hunk + hunk(b'BMYS', symb_pl)
    return b'TSFB' + B(len(body)) + body + bytes(1024)

open(out + '/C.tkl', 'wb').write(tsfb_tkl(hunk(b'TCES', tsect), tsymb))
