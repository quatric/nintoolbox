#!/usr/bin/env python3
# Synthetic Capcom archives (Dead Rising Wii): t.lpk (@LPK, two files), t.fpk (@FPK, two groups, one
# "bres" member and one raw member) and t.ana (ANA@, two named entries).
import struct, sys, os
d = sys.argv[1]
A = b'aaaa1234' * 4
B = b'bres' + bytes(range(28))
# @LPK
ents = [(b'one.bin', A), (b'two.bin', B)]
off = 0x10 + 0x30 * len(ents)
hdr = b'@LPK' + struct.pack('>III', 0, len(ents), 0)
body = b''
for n, c in ents:
    hdr += n.ljust(0x28, b'\0') + struct.pack('>II', len(c), off + len(body))
    body += c
open(os.path.join(d, 't.lpk'), 'wb').write(hdr + body)
# @FPK: group 0 holds B, group 5 holds A
tab0, tab5 = 0x30, 0x40
off0, off5 = 0x50, 0x50 + len(B)
f = b'@FPK' + struct.pack('>III', 0xffff, 2, 0)
f += struct.pack('>IIII', 0, 1, tab0, 0) + struct.pack('>IIII', 5, 1, tab5, 0)
f += struct.pack('>IIII', 0, off0, 0xffffffff, 0xffffffff) + struct.pack('>IIII', 0, off5, 0xffffffff, 0xffffffff)
f += B + A
open(os.path.join(d, 't.fpk'), 'wb').write(f)
# ANA@
n = 2
o0 = 0x30 + 0x10 * n
a = b'ANA@' + struct.pack('>II', 0x20, 0) + bytes(0x14) + struct.pack('>I', n) + bytes(12)
a += struct.pack('>II', o0, len(B)) + b'tx_000'.ljust(8, b'\0')
a += struct.pack('>II', o0 + len(B), len(A)) + b'pic'.ljust(8, b'\0')
a += B + A
open(os.path.join(d, 't.ana'), 'wb').write(a)
