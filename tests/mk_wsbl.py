#!/usr/bin/env python3
# Synthetic 49Games WSBL bank: 2 mono DSP-ADPCM sounds.
import struct, sys, os
recs = b''; data = b''
for i, ns in enumerate((28, 14)):
    r = bytearray(0x8c)
    struct.pack_into('>II', r, 4, 1, 1)
    struct.pack_into('>IfII', r, 0x0c, 32000, 1.0, len(data), ns)
    struct.pack_into('>16h', r, 0x34, *range(1, 17))
    recs += r
    data += (bytes([0x10 + i]) + bytes(7)) * ((ns + 13) // 14)
hdr = b'WSBL' + struct.pack('>IIII', 1, 2, len(data), 0)
open(os.path.join(sys.argv[1], 't.wsbl'), 'wb').write(hdr + recs + data)
