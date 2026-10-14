#!/usr/bin/env python3
# Synthetic EA SCHl stream t.exa (Wii platform, version 3, EA-XA): one SCDl block with one ADPCM frame of 28 samples.
import struct, sys, os
patches = bytes([0x06, 0x01, 0x65, 0xfd, 0x80, 0x01, 0x03, 0x82, 0x01, 0x01, 0x84, 0x02, 0x5d, 0xc0, 0x85, 0x01, 0x1c,
                 0xa0, 0x01, 0x0a, 0xff])
hdr = b'PT' + struct.pack('<H', 6) + patches
hdr = hdr.ljust((len(hdr) + 3) & ~3, b'\0')
schl = b'SCHl' + struct.pack('<I', 8 + len(hdr)) + hdr
frame = bytes([0x00]) + bytes([0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc])
data = struct.pack('>II', 28, 0) + frame
data = data.ljust((len(data) + 3) & ~3, b'\0')
scdl = b'SCDl' + struct.pack('<I', 8 + len(data)) + data
scel = b'SCEl' + struct.pack('<I', 8)
open(os.path.join(sys.argv[1], 't.exa'), 'wb').write(schl + scdl + scel)
