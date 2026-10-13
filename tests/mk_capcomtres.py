#!/usr/bin/env python3
# Synthetic Capcom TRES text resource: t.txtres with two records (one labelled, one with two strings)
import struct, sys, os
d = sys.argv[1]
n = 2
tab = 0x10 + 4 * n
r0, r1 = tab, tab + 20
pool = tab + 40
s_lab, s_a, s_b, s_c = pool, pool + 4, pool + 4 + 6, pool + 4 + 6 + 3
rec0 = struct.pack('>5I', 1, 0, 1, s_a, 0)
rec1 = struct.pack('>5I', 2, s_lab, 2, s_b, 0)
p = b'LB1\0' + b'Hello\0' + b'a\nb\0' + b'tail\0'
s_a, s_b, s_c = pool + 4, pool + 4 + 6, pool + 4 + 6 + 4
rec0 = struct.pack('>5I', 1, 0, 1, s_a, 0)
rec1 = struct.pack('>5I', 2, pool, 2, s_b, 0)
hdr = b'TRES' + struct.pack('>III', 0x10000, n, 0) [:12]
hdr = b'TRES' + struct.pack('>II', 0x10000, n) + struct.pack('>I', 0x10) + struct.pack('>II', r0, r1)
open(os.path.join(d, 't.txtres'), 'wb').write(hdr + rec0 + rec1 + p)
