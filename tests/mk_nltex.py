#!/usr/bin/env python3
# Synthetic Next Level Games texture banks (Mario Strikers Charged, Wii): one 8x8 CMPR texture (solid red) in a
# PTLG ".rlt" (t.rlt) and in a ".res" resource file (t.res).
import struct, sys, os
d = sys.argv[1]
tex = struct.pack('>II', 1, 2) + bytes([5, 6, 5, 0]) + struct.pack('>HHHH', 0, 8, 8, 0) + bytes(12)
tex += (struct.pack('>HHI', 0xf800, 0x0000, 0)) * 4
assert len(tex) == 0x20 + 32
rlt = b'PTLG' + struct.pack('>III', 1, 0x1234, 0) + struct.pack('>4I', 0xcafe0001, 0, len(tex), 0) + tex
open(os.path.join(d, 't.rlt'), 'wb').write(rlt)
res = struct.pack('>4I', 0x20, 1, 1, 6) + bytes(16) + struct.pack('>3I', 0xcafe0002, 3, len(tex)) + bytes(0x60 - 0x2c) + tex
open(os.path.join(d, 't.res'), 'wb').write(res)
