#!/usr/bin/env python3
# Synthetic Krome .tex t.tex: 8x8 RGBA8 (format 0), one mip level.
import struct, sys, os
d = sys.argv[1]
data = bytes((i * 7) & 255 for i in range(256))
hdr = struct.pack('>8I', 0, 8, 8, len(data), 0, 0, 0, 0)
open(os.path.join(d, 't.tex'), 'wb').write(hdr + data)
