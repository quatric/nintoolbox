#!/usr/bin/env python3
# Synthetic Nintendo Channel HDPK001B schema: header, padding, names, trailer markers.
import struct, sys, os
names = b'Tbl\0col\0idx\0'
body = b'\0' * (0x40 - 0x1c) + names + b'CHECKDATA\0VERSION\0'
size = 0x1c + len(body)
hdr = b'HDPK001B' + struct.pack('>III', size, 0x40, 2) + b'\0' * 8
hdr += b'\0' * (0x1c - len(hdr))
open(os.path.join(sys.argv[1], 'h.bin'), 'wb').write(hdr + body)
